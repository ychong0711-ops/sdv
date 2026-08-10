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
    LOG_INF("SOME/IP listening on UDP %d (Service 0x%04X)", SOMEIP_PORT, SERVICE_ID);
    return 0;
#endif
}

int someip_offer_service(void) {
    // 정보용 로그: Service Discovery(OfferService)는 SD 미구현.
    // 실제로는 vsomeip-routing(SD port 30491)이 담당하나, 여기서는 정적 수신만 수행
    // (포트폴리오 범위: 주기적 multicast Offer는 구현하지 않음).
    LOG_INF("SOME/IP OfferService: 0x%04X/0x%04X Events 0x%04X/0x%04X (SD 미구현 - 수신 전용)",
            SERVICE_ID, INSTANCE_ID, EVENT_NOTIFY, EVENT_HEARTBEAT);
    return 0;
}

#ifndef CONFIG_SOMEIP_TRANSPORT_UART
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
