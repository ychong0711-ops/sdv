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

// SOME/IP-SD (AUTOSAR PRS SOME/IP-SD, vsomeip 호환) - 수신 처리용
#define SD_SERVICE_ID 0xFFFF
#define SD_METHOD_ID 0x8100
#define SD_MULTICAST_IP "224.224.224.245"
#define SD_MULTICAST_ADDR 0xE0E0E0F5 /* 224.224.224.245 (네트워크 오더 변환 전) */
// Entry type
#define SD_ENTRY_FIND_SERVICE 0x00
#define SD_ENTRY_OFFER_SERVICE 0x01 /* TTL=0이면 StopOffer */
#define SD_ENTRY_SUBSCRIBE_EVENTGROUP 0x06
#define SD_ENTRY_SUBSCRIBE_EVENTGROUP_ACK 0x07
// Option type
#define SD_OPT_IP4_ENDPOINT 0x04

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

// SD 전용 헤더 (SOME/IP 헤더 16B 뒤, flags 1B + reserved 3B + entries_len 4B = 8B)
// entries 뒤에 options_len u32 + options 가 이어진다.
struct sd_header {
    uint8_t flags;          // bit7 reboot, bit6 unicast
    uint8_t reserved[3];
    uint32_t entries_len;   // entries 배열 바이트 수 (네트워크 오더)
} __attribute__((packed));

// SD Service/Eventgroup Entry (16 bytes, AUTOSAR)
// type 0x00 Find / 0x01 Offer / 0x06 Subscribe / 0x07 SubscribeAck
struct sd_entry {
    uint8_t type;
    uint8_t index1;         // 첫 옵션 런 시작 인덱스 (옵션 배열 0-based)
    uint8_t index2;         // 두 번째 옵션 런 시작 인덱스
    uint8_t n_opts;         // 상위 니블 = 런1 개수, 하위 니블 = 런2 개수
    uint16_t service_id;
    uint16_t instance_id;
    uint8_t major_version;
    uint8_t ttl[3];         // 24-bit TTL
    uint32_t minor_version; // 서비스 엔트리용 (이벤트그룹 엔트리는 reserved u16 + eventgroup_id u16)
} __attribute__((packed));

// IPv4 Endpoint Option (12 bytes) - length 필드 = 9
struct sd_ipv4_endpoint_option {
    uint16_t length;    // 9
    uint8_t type;       // 0x04 = IP4_ENDPOINT
    uint8_t reserved;   // discardable
    uint8_t address[4]; // IPv4 주소
    uint8_t reserved2;
    uint8_t l4_proto;   // 0x11 = UDP
    uint16_t port;
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
    // SD 메시지(OfferService/SubscribeEventgroup)는 someip_sd_handle()에서 수신 처리한다.
    // 프로바이더(MPU)는 vsomeip 호환 SD 광고를 멀티캐스트로 주기 전송.
    LOG_INF("SOME/IP OfferService: 0x%04X/0x%04X Events 0x%04X/0x%04X (SD 수신 전용)",
            SERVICE_ID, INSTANCE_ID, EVENT_NOTIFY, EVENT_HEARTBEAT);
    return 0;
}

#ifndef CONFIG_SOMEIP_TRANSPORT_UART

/** 빅엔디안 u16/u32 바이트 조립 (정렬 안전, 패킷 버퍼에서 unaligned read 회피) */
static uint16_t sd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint32_t sd_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/**
 * SD(Service Discovery) 메시지 수신 처리.
 *
 * 와이어 포맷 (vsomeip 호환, 빅엔디안):
 *   SOME/IP 헤더 16B (service 0xFFFF, method 0x8100, type 0x02)
 *   SD 헤더 8B: flags u8 | reserved 3B | entries_len u32
 *   entries (16B 단위) | options_len u32 | options
 *
 * OfferService(0x01) 수신 시 우리 서비스(0x1234/0x5678)를 찾아
 * IPv4 Endpoint 옵션(0x04)에서 프로바이더 주소를 로그로 남긴다.
 * SubscribeEventgroup(0x06) 수신 시 이벤트그룹 구독 요청을 로그로 남긴다.
 * SD 메시지는 이벤트가 아니므로 항상 -1을 반환한다 (이벤트 처리 생략).
 */
static int someip_sd_handle(const uint8_t *buf, ssize_t len) {
    const size_t hdr_size = sizeof(struct someip_header) + sizeof(struct sd_header);
    if (len < (ssize_t)(hdr_size + 4)) {
        return -1; // SD 헤더 + entries_len 최소 크기 미달
    }

    if (sd_u16(buf) != SD_SERVICE_ID) return -1;
    if (sd_u16(buf + 2) != SD_METHOD_ID) return -1;

    const uint8_t *sd = buf + sizeof(struct someip_header);
    uint32_t entries_len = sd_u32(sd + 4);
    if (entries_len % sizeof(struct sd_entry) != 0) return -1; // 엔트리 16B 배수 검증

    const uint8_t *entries = buf + hdr_size;
    if (entries_len > (uint32_t)(len - hdr_size - 4)) return -1; // options_len 필드까지 확보

    uint32_t options_len = sd_u32(entries + entries_len);
    const uint8_t *options = entries + entries_len + 4;
    if (options_len > (uint32_t)(len - hdr_size - 4 - entries_len)) return -1;

    for (uint32_t off = 0; off < entries_len; off += sizeof(struct sd_entry)) {
        const uint8_t *e = entries + off;
        uint8_t type = e[0];
        uint16_t svc_id = sd_u16(e + 4);
        uint16_t inst_id = sd_u16(e + 6);
        uint8_t major = e[8];
        uint32_t ttl = ((uint32_t)e[9] << 16) | ((uint32_t)e[10] << 8) | e[11];

        if (type == SD_ENTRY_OFFER_SERVICE) {
            if (ttl == 0) {
                LOG_INF("SOME/IP SD: StopOffer 0x%04X/0x%04X", svc_id, inst_id);
                continue;
            }
            if (svc_id != SERVICE_ID || inst_id != INSTANCE_ID) continue;

            // entry.index1이 가리키는 옵션으로 이동 (vsomeip는 옵션 연속 배치,
            // length 필드는 type 제외 -> 블록 크기 = 3 + length)
            uint8_t index1 = e[1];
            const uint8_t *opt = options;
            uint32_t remaining = options_len;
            for (uint8_t i = 0; i < index1 && remaining >= 3; i++) {
                uint32_t block = 3u + sd_u16(opt);
                if (block > remaining) break;
                opt += block;
                remaining -= block;
            }
            if (remaining >= sizeof(struct sd_ipv4_endpoint_option) &&
                sd_u16(opt) == 9 && opt[2] == SD_OPT_IP4_ENDPOINT) {
                char ip_str[NET_IPV4_ADDR_LEN];
                net_addr_ntop(AF_INET, opt + 4, ip_str, sizeof(ip_str));
                uint16_t port = sd_u16(opt + 10);
                LOG_INF("SOME/IP SD: OfferService 0x%04X/0x%04X @ %s:%u (major %u)",
                        svc_id, inst_id, ip_str, port, major);
            }
        } else if (type == SD_ENTRY_SUBSCRIBE_EVENTGROUP) {
            // 이벤트그룹 엔트리: offset 12~13 reserved u16, 14~15 eventgroup_id u16
            uint16_t eg_id = sd_u16(e + 14);
            LOG_INF("SOME/IP SD: SubscribeEventgroup 0x%04X/0x%04X eg 0x%04X (major %u)",
                    svc_id, inst_id, eg_id, major);
        }
    }
    return -1; // SD 메시지는 이벤트 아님
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
