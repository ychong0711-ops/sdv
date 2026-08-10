#pragma once
#include <stdint.h>

// SOME/IP Light Stack for Zephyr (vsomeip-compatible)
// Service: DriverMonitoringService 0x1234, Instance 0x5678, Event 0x8001 (NotifyDrowsiness)
// Transport: UDP unicast 30490 (PC simulation) / UART tunnel (real board, CONFIG_SOMEIP_TRANSPORT_UART=y)

int someip_init(void);
int someip_offer_service(void);          // 정보용 로그 (SD 수신/발신 클라이언트 역할 명시)
int someip_receive(uint16_t *event_id, uint8_t *payload); // 성공 0, 실패/타임아웃 -1
int someip_sd_send_find(void);           // FindService 멀티캐스트 전송 (소비자 탐색)
int someip_sd_send_subscribe(void);      // SubscribeEventgroup 전송 (이벤트그룹 구독)
