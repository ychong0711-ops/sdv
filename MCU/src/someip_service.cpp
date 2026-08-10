/**
 * SOME/IP Light Stack - Zephyr 구현
 *
 * 설계: App Lab Bridge (proprietary)를 vsomeip 호환 SOME/IP로 재구현
 * - Service: DriverMonitoringService 0x1234 / Instance 0x5678
 * - Event: NotifyDrowsiness (0x8001), Heartbeat (0x8002)
 * - Transport: UDP unicast 30490 (기본, PC 시뮬레이션) / UART tunnel (실보드)
 *
 * 실제 vsomeip는 Linux용이라 Zephyr에서는 경량 SOME/IP 파서 구현.
 * 패킷 포맷은 vsomeip와 호환 - Wireshark에서 SOME/IP로 인식됨.
 *
 * SOME/IP 헤더 16바이트 (빅엔디안):
 *   service_id u16 | method_id u16 | length u32 | client_id u16 | session_id u16 |
 *   protocol_version u8 | interface_version u8 | message_type u8 | return_code u8
 *   length = payload 길이 + 8 (1바이트 payload면 length=9)
 *   UDP 데이터그램은 8바이트 정렬: 16헤더 + 1payload = 17 -> +7 패딩 = 총 24 (패딩은 length에 미포함)
 */

#include "someip_service.h"
#include "someip_sd.h" /* SD 프로토콜: 상수/빌더/파서 (순수 C, 호스트 테스트 대상) */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/net_ip.h> /* net_ntohl/net_ntohs, net_addr_ntop (Zephyr main) */
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(someip, LOG_LEVEL_INF);

#define SOMEIP_PORT 30490
#define SERVICE_ID 0x1234
#define INSTANCE_ID 0x5678
#define EVENT_NOTIFY 0x8001
#define EVENT_HEARTBEAT 0x8002
#define SD_EVENTGROUP_ID 0x0001 /* Notify/Heartbeat 이벤트 그룹 (구독 대상) */

// SD 멀티캐스트 전송/수신 주소 (일부만 someip_sd.h에 없어 여기 유지)
#define SD_MULTICAST_IP "224.224.224.245"
#define SD_MULTICAST_ADDR 0xE0E0E0F5 /* 224.224.224.245 (네트워크 오더 변환 전) */

// SOME/IP Header (AUTOSAR SWS, 16 bytes) - 네트워크(빅엔디안) 바이트 오더
struct someip_header {
    uint16_t service_id;
    uint16_t method_id; // Event ID (0x8001 / 0x8002)
    uint32_t length;    // payload + 8
    uint16_t client_id;
    uint16_t session_id;
    uint8_t protocol_version;
    uint8_t interface_version;
    uint8_t message_type; // 0x02 = Notification
    uint8_t return_code;
} __attribute__((packed));

#ifndef CONFIG_SOMEIP_TRANSPORT_UART
#include <zephyr/net/socket.h>
static int sock = -1;
#endif

#ifdef CONFIG_SOMEIP_TRANSPORT_UART
#define SOMEIP_UART_NODE DT_ALIAS(someip-uart)
static const struct device *uart_dev = DEVICE_DT_GET(SOMEIP_UART_NODE);
static uint8_t rx_buf[64];
static size_t rx_len = 0;
static int64_t last_rx_tick = 0;

/**
 * UART RX 상태 머신: 바이트 스트림에서 SOME/IP 프레임 재조립.
 * 1) 16바이트 헤더 확보 -> 2) header.length 필드로 payload 길이 계산
 *    (전체 프레임 = 16 + (length - 8)) -> 3) 완전 프레임 수신 시 파싱/반환.
 * 불완전 프레임 상태로 50ms 이상 데이터가 없으면 버퍼 리셋 (스트림 재동기).
 */
static int someip_uart_receive(uint16_t *event_id, uint8_t *payload) {
    while (1) {
        // 스트림 재동기: 불완전 프레임 50ms 데이터 부재 시 리셋
        if (rx_len > 0 && (k_uptime_get() - last_rx_tick) > 50) {
            LOG_WRN("SOME/IP UART: partial frame (%u B) timeout -> resync",
                    (unsigned)rx_len);
            rx_len = 0;
        }

        uint8_t byte;
        while (uart_poll_in(uart_dev, &byte) == 0) {
            last_rx_tick = k_uptime_get();
            if (rx_len >= sizeof(rx_buf)) {
                rx_len = 0; // 버퍼 오버플로우 -> 재동기
                continue;
            }
            rx_buf[rx_len++] = byte;

            if (rx_len >= sizeof(struct someip_header)) {
                auto *hdr = reinterpret_cast<struct someip_header *>(rx_buf);
                uint32_t length = net_ntohl(hdr->length);
                if (length < 8 || length > 32) {
                    LOG_WRN("SOME/IP UART: invalid length %u -> resync", length);
                    rx_len = 0;
                    continue;
                }
                size_t total = sizeof(struct someip_header) + (length - 8);
                if (rx_len >= total) {
                    // 완전 프레임 수신 -> 공통 검증 (service_id/message_type/length)
                    if (net_ntohs(hdr->service_id) == SERVICE_ID &&
                        hdr->message_type == 0x02) {
                        *event_id = net_ntohs(hdr->method_id);
                        *payload = rx_buf[sizeof(struct someip_header)];
                        rx_len = 0;
                        return 0;
                    }
                    LOG_WRN("SOME/IP UART: invalid frame (svc 0x%04X type %u) -> skip",
                            net_ntohs(hdr->service_id), hdr->message_type);
                    rx_len = 0;
                }
            }
        }
        k_sleep(K_MSEC(10));
    }
}
#endif

int someip_init(void) {
#ifdef CONFIG_SOMEIP_TRANSPORT_UART
    // UART 전송 (실보드: UNO Q LPUART1 터널). 115200은 보드 기본 설정 사용.
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("SOME/IP UART device not ready (DT_ALIAS someip-uart)");
        return -1;
    }
    LOG_INF("SOME/IP transport: UART (%s, 115200)", uart_dev->name);
    return 0;
#else
    // UDP socket for SOME/IP (PC 시뮬레이션: 127.0.0.1:30490)
    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("SOME/IP socket failed: %d", sock);
        return -1;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SOMEIP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (zsock_bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        LOG_ERR("SOME/IP bind failed");
        return -1;
    }
    // SD(Service Discovery) 수신: OfferService가 멀티캐스트 224.224.224.245:30490으로
    // 오므로 멀티캐스트 그룹에 join한다. (vsomeip와 동일 포트, 동일 소켓에서 구분 수신)
    struct ip_mreq mreq = {0};
    mreq.imr_multiaddr.s_addr = htonl(SD_MULTICAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (zsock_setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        LOG_WRN("SOME/IP SD multicast join failed (OfferService 수신 불가)");
    } else {
        LOG_INF("SOME/IP SD multicast joined: %s:%d", SD_MULTICAST_IP, SOMEIP_PORT);
    }
    LOG_INF("SOME/IP listening on UDP %d (Service 0x%04X)", SOMEIP_PORT, SERVICE_ID);
    return 0;
#endif
}

int someip_offer_service(void) {
    // MCU는 소비자(Consumer) 역할: 주기적 multicast OfferService 광고는 하지 않고,
    // someip_sd_send_find()로 서비스를 탐색하고, OfferService 수신 시
    // someip_sd_send_subscribe()로 이벤트그룹을 구독한다 (SD 클라이언트 흐름).
    LOG_INF("SOME/IP OfferService: 0x%04X/0x%04X Events 0x%04X/0x%04X (SD client)",
            SERVICE_ID, INSTANCE_ID, EVENT_NOTIFY, EVENT_HEARTBEAT);
    return 0;
}

#ifndef CONFIG_SOMEIP_TRANSPORT_UART

/**
 * SD(Service Discovery) 메시지 수신 처리.
 *
 * 파싱은 someip_sd.h의 sd_parse() (순수 C, 호스트 테스트로 wire format 검증됨)에 위임.
 *
 * OfferService(0x01) 수신 시 우리 서비스(0x1234/0x5678)를 찾아
 * IPv4 Endpoint 옵션(0x04)에서 프로바이더 주소를 로그로 남기고,
 * SubscribeEventgroup(0x06) 수신 시 이벤트그룹 구독 요청을 로그로 남긴다.
 * SD 메시지는 이벤트가 아니므로 항상 -1을 반환한다 (이벤트 처리 생략).
 */
static int someip_sd_handle(const uint8_t *buf, ssize_t len) {
    struct sd_parsed parsed;
    if (sd_parse(buf, (size_t)len, &parsed) < 0) {
        return -1;
    }

    for (uint8_t i = 0; i < parsed.entry_count; i++) {
        const struct sd_entry *e = &parsed.entries[i];

        if (e->type == SD_ENTRY_OFFER_SERVICE) {
            if (e->ttl == 0) {
                LOG_INF("SOME/IP SD: StopOffer 0x%04X/0x%04X",
                        e->service_id, e->instance_id);
                continue;
            }
            if (e->service_id != SERVICE_ID || e->instance_id != INSTANCE_ID) {
                continue;
            }
            // OfferService의 첫 IPv4 Endpoint 옵션에서 프로바이더 주소 추출
            for (uint8_t j = 0; j < parsed.option_count; j++) {
                const struct sd_option_ip4 *o = &parsed.options[j];
                if (o->type == SD_OPT_IP4_ENDPOINT && o->l4_proto == SD_L4_UDP) {
                    char ip_str[NET_IPV4_ADDR_LEN];
                    net_addr_ntop(AF_INET, o->address, ip_str, sizeof(ip_str));
                    LOG_INF("SOME/IP SD: OfferService 0x%04X/0x%04X @ %s:%u (major %u)",
                            e->service_id, e->instance_id, ip_str, o->port,
                            e->major_version);
                    // 소비자 흐름 완성: Find -> Offer 수신 -> Subscribe 전송
                    someip_sd_send_subscribe();
                    break;
                }
            }
        } else if (e->type == SD_ENTRY_SUBSCRIBE_EVENTGROUP) {
            LOG_INF("SOME/IP SD: SubscribeEventgroup 0x%04X/0x%04X eg 0x%04X (major %u)",
                    e->service_id, e->instance_id, e->eventgroup_id,
                    e->major_version);
        }
    }
    return -1; // SD 메시지는 이벤트 아님
}

/**
 * FindService 전송: 멀티캐스트 224.224.224.245:30490으로 서비스 탐색 요청.
 * (소비자 역할 - vsomeip 호환 SD 클라이언트)
 */
int someip_sd_send_find(void) {
    uint8_t buf[64];
    static uint16_t session_id = 0;
    int pkt_len = sd_build_find_service(buf, sizeof(buf), SERVICE_ID, INSTANCE_ID,
                                        ++session_id);
    if (pkt_len < 0) {
        LOG_WRN("SOME/IP SD: FindService build failed");
        return -1;
    }
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(SOMEIP_PORT);
    dst.sin_addr.s_addr = htonl(SD_MULTICAST_ADDR);
    int ret = zsock_sendto(sock, buf, (size_t)pkt_len, 0,
                           reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
    if (ret == pkt_len) {
        LOG_INF("SOME/IP SD: FindService 0x%04X/0x%04X sent (session %u)",
                SERVICE_ID, INSTANCE_ID, session_id);
        return 0;
    }
    LOG_WRN("SOME/IP SD: FindService send failed (%d)", ret);
    return -1;
}

/**
 * SubscribeEventgroup 전송: 이벤트그룹 0x0001 구독 요청 (프로바이더 유니캐스트).
 * 소비자 역할 완성: Find -> Offer 수신 -> Subscribe.
 */
int someip_sd_send_subscribe(void) {
    uint8_t buf[64];
    static uint16_t session_id = 0;
    // 구독 요청자의 수신 엔드포인트 = 로컬 (UDP 30490). PC 시뮬레이션 기준.
    const uint8_t self_ip[4] = {127, 0, 0, 1};
    int pkt_len = sd_build_subscribe_eventgroup(
        buf, sizeof(buf), SERVICE_ID, INSTANCE_ID, SD_EVENTGROUP_ID,
        self_ip, SOMEIP_PORT, ++session_id);
    if (pkt_len < 0) {
        LOG_WRN("SOME/IP SD: SubscribeEventgroup build failed");
        return -1;
    }
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(SOMEIP_PORT);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 프로바이더 = MPU (PC 시뮬레이션)
    int ret = zsock_sendto(sock, buf, (size_t)pkt_len, 0,
                           reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
    if (ret == pkt_len) {
        LOG_INF("SOME/IP SD: SubscribeEventgroup eg 0x%04X sent (session %u)",
                SD_EVENTGROUP_ID, session_id);
        return 0;
    }
    LOG_WRN("SOME/IP SD: SubscribeEventgroup send failed (%d)", ret);
    return -1;
}

static int someip_udp_receive(uint16_t *event_id, uint8_t *payload) {
    uint8_t buf[64];
    struct sockaddr_in src;
    socklen_t len = sizeof(src);
    // Non-blocking with timeout 50ms
    struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};
    zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int ret = zsock_recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&src), &len);
    if (ret < (int)sizeof(struct someip_header) + 1) return -1;

    auto *hdr = reinterpret_cast<struct someip_header *>(buf);
    // SD(Service Discovery) 메시지 라우팅: service 0xFFFF는 이벤트가 아니므로 별도 처리
    if (net_ntohs(hdr->service_id) == SD_SERVICE_ID) {
        return someip_sd_handle(buf, ret);
    }

    // 공통 검증: service_id==0x1234 && message_type==0x02 && length sanity (>=8 && <=32)
    uint32_t length = net_ntohl(hdr->length);
    if (net_ntohs(hdr->service_id) != SERVICE_ID) return -1;
    if (hdr->message_type != 0x02) return -1;
    if (length < 8 || length > 32) return -1;

    *event_id = net_ntohs(hdr->method_id); // 호스트 바이트 오더
    *payload = buf[sizeof(struct someip_header)]; // payload 첫 바이트
    char src_str[NET_IPV4_ADDR_LEN];
    net_addr_ntop(AF_INET, &src.sin_addr, src_str, sizeof(src_str));
    LOG_DBG("SOME/IP Notify RX: event=0x%04X level=%d from %s",
            *event_id, *payload, src_str);
    return 0;
}
#endif

// UART/UDP 공통 진입점
int someip_receive(uint16_t *event_id, uint8_t *payload) {
#ifdef CONFIG_SOMEIP_TRANSPORT_UART
    return someip_uart_receive(event_id, payload);
#else
    return someip_udp_receive(event_id, payload);
#endif
}

#ifdef CONFIG_SOMEIP_TRANSPORT_UART
/* SD 발신은 UDP 멀티캐스트 전용 (PC 시뮬레이션). 실보드(UART 터널)에서는 미지원.
 * 헤더 선언과 링크를 맞추기 위한 스텁. */
int someip_sd_send_find(void) {
    LOG_INF("SOME/IP SD: FindService over UART not supported");
    return -1;
}

int someip_sd_send_subscribe(void) {
    LOG_INF("SOME/IP SD: SubscribeEventgroup over UART not supported");
    return -1;
}
#endif
