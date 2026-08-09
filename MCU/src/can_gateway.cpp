#include "can_gateway.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(can_gw, LOG_LEVEL_DBG);

static uint8_t alive_counter = 0;

uint8_t e2e_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x1D;
            else crc <<= 1;
        }
    }
    return crc ^ 0xFF;
}

// 공통 CAN-FD 프레임 전송: [0x55][0xAA][0x01][level][0x00][0x00][CRC][Counter]
static int can_send_frame(const struct device *can_dev, uint8_t level, int32_t timeout_ms) {
    struct can_frame frame = {0};
    frame.id = 0x18FF01F4;
    frame.flags = CAN_FRAME_IDE | CAN_FRAME_FDF | CAN_FRAME_BRS; // Extended + CAN-FD + BRS
    frame.dlc = 8;
    // Data: [Header 0x55][Header 0xAA][Version 0x01][Level][Reserved][Reserved][CRC][Counter]
    frame.data[0] = 0x55;
    frame.data[1] = 0xAA;
    frame.data[2] = 0x01;
    frame.data[3] = level;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = e2e_crc8(frame.data, 6); // E2E Protection
    frame.data[7] = alive_counter++;

    int ret = can_send(can_dev, &frame, K_MSEC(timeout_ms), NULL, NULL);
    if (ret == 0) {
        LOG_INF("CAN TX: ID 0x%08X DLC %d Data %02X %02X %02X %02X CRC %02X Ctr %02X",
                frame.id, frame.dlc,
                frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                frame.data[6], frame.data[7]);
    } else {
        LOG_ERR("CAN TX failed: %d", ret);
    }
    return ret;
}

int can_send_drowsiness(const struct device *can_dev, uint8_t level) {
    // CAN thread 컨텍스트 - 100ms 타임아웃 유지
    return can_send_frame(can_dev, level, 100);
}

int can_send_warning(const struct device *can_dev) {
    // safety 타이머 콜백(workqueue)에서 호출되므로 K_MSEC(5) 이하 타임아웃 사용
    return can_send_frame(can_dev, 0xFF, 5);
}
