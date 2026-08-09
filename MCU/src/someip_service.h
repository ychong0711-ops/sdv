#pragma once
#include <stdint.h>

// SOME/IP Light Stack for Zephyr (vsomeip-compatible)
// Service: DriverMonitoringService 0x1234, Instance 0x5678, Event 0x8001 (NotifyDrowsiness)
// Transport: UDP unicast 30490 (PC simulation) / UART tunnel (real board, CONFIG_SOMEIP_TRANSPORT_UART=y)

int someip_init(void);
int someip_offer_service(void);          // 정보용 로그 (SD 미구현 명시 주석 유지)
int someip_receive(uint16_t *event_id, uint8_t *payload); // 성공 0, 실패/타임아웃 -1
