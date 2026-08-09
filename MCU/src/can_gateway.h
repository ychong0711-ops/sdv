#pragma once
#include <zephyr/drivers/can.h>
#include <stdint.h>

// CAN-FD: Drowsiness 경고 프레임
// ID: 0x18FF01F4 (J1939 Proprietary A, Priority 6)
// DLC: 8, E2E Protection: CRC8 SAE J1850 + Alive Counter
int can_send_drowsiness(const struct device *can_dev, uint8_t level);

// CAN-FD: Safe-State 경고 프레임 (level=0xFF, 데이터 레이아웃 동일, CRC/카운터 포함)
int can_send_warning(const struct device *can_dev);

// E2E CRC8 (SAE J1850, poly 0x1D) - ISO 26262 E2E Profile 1
uint8_t e2e_crc8(const uint8_t *data, size_t len);
