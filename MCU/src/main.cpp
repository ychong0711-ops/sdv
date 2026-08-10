/**
 * MCU: STM32U585 (Cortex-M33) + Zephyr RTOS
 * Heterogeneous SDV Zone Controller - Real-Time Core
 * 
 * Rebranded: Arduino UNO Q MCU side, but NO Arduino abstraction used.
 * Build: west build -b arduino_uno_q ./MCU
 * 
 * Features:
 *  - Zephyr RTOS (threads, timers, watchdog)
 *  - SOME/IP light stack (raw-socket 경량 구현)
 *  - CAN-FD TX with E2E CRC8 (ISO 26262)
 *  - Safe State on MPU heartbeat timeout
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "can_gateway.h"
#include "someip_service.h"
#include "safety.h"

LOG_MODULE_REGISTER(mcu_main, LOG_LEVEL_INF);

#define CAN_DEVICE_NODE DT_CHOSEN(zephyr_canbus)
static const struct device *can_dev = DEVICE_DT_GET(CAN_DEVICE_NODE);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

// SOME/IP Events (공통 상수)
#define EVENT_NOTIFY 0x8001
#define EVENT_HEARTBEAT 0x8002

// Zephyr Threads: CAN, SOME/IP, Safety 각각 분리 (Freedom from Interference)
#define STACK_SIZE 2048
K_THREAD_STACK_DEFINE(can_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(someip_stack, STACK_SIZE);
static struct k_thread can_thread_data;
static struct k_thread someip_thread_data;

// Shared: MPU에서 받은 졸음 레벨 (0-100)
static atomic_t drowsiness_level = ATOMIC_INIT(0);
static atomic_t mpu_heartbeat = ATOMIC_INIT(0);

void can_thread(void *, void *, void *) {
    LOG_INF("CAN thread started (Prio 5)");
    while (1) {
        int level = atomic_get(&drowsiness_level);
        if (level > 0) {
            // CAN-FD 프레임: ID 0x18FF01F4 (J1939 확장), DLC 8
            // Data: [0x55, 0xAA, 0x01, level, 0x00, 0x00, CRC, counter]
            can_send_drowsiness(can_dev, (uint8_t)level);
            atomic_set(&drowsiness_level, 0);
        }
        k_msleep(10); // 10ms 주기 - Real-Time
    }
}

void someip_thread(void *, void *, void *) {
    LOG_INF("SOME/IP thread started (Prio 6)");
    someip_init(); // Offer Service 0x1234/0x5678
    someip_offer_service();

    uint32_t last_find = 0;
    while (1) {
        // SD 클라이언트: 2초마다 FindService 재전송 (프로바이더 재시작 대응)
        if (k_uptime_get_32() - last_find >= 2000) {
            someip_sd_send_find();
            last_find = k_uptime_get_32();
        }

        // SOME/IP 수신 대기 (blocking): Notify(0x8001) / Heartbeat(0x8002) 공통 진입점
        uint16_t event_id;
        uint8_t payload;
        if (someip_receive(&event_id, &payload) == 0) {
            atomic_set(&mpu_heartbeat, k_uptime_get_32());
            if (event_id == EVENT_NOTIFY) {
                atomic_set(&drowsiness_level, payload);
                LOG_INF("SOME/IP RX: drowsiness=%u%%", payload);
            }
            // EVENT_HEARTBEAT(0x8002): heartbeat 전용 (추가 처리 없음)
            safety_kick_watchdog(); // notify/heartbeat 수신 시 safety 상태 유지
        }
    }
}

int main(void) {
    LOG_INF("=== SDV Zone Controller (STM32U585 + Zephyr) Boot ===");
    LOG_INF("Build: %s %s", __DATE__, __TIME__);

    // 1. GPIO (LED for Safe State)
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED GPIO not ready");
        return 0;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    // 2. CAN-FD 초기화 (500k arbit, 2M data)
    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
        return 0;
    }
    struct can_timing timing;
    struct can_timing timing_data;
    // arbitration 500k (75% sample point) / data 2M (87.5%) — CAN-FD dual timing
    if (can_calc_timing(can_dev, &timing, 500000, 750) != 0 ||
        can_set_timing(can_dev, &timing) != 0) {
        LOG_ERR("CAN arbitration timing config failed");
        return 0;
    }
    if (can_calc_timing_data(can_dev, &timing_data, 2000000, 875) != 0 ||
        can_set_timing_data(can_dev, &timing_data) != 0) {
        LOG_ERR("CAN data timing config failed");
        return 0;
    }
    can_set_mode(can_dev, CAN_MODE_NORMAL);
    can_start(can_dev);
    LOG_INF("CAN-FD started: 500k/2M");

    // 3. Safety (Watchdog 100ms) - CAN + LED 참조 전달
    safety_init(can_dev, &led);

    // 4. Threads 생성 (Zephyr RTOS)
    k_thread_create(&can_thread_data, can_stack, K_THREAD_STACK_SIZEOF(can_stack),
                    can_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_create(&someip_thread_data, someip_stack, K_THREAD_STACK_SIZEOF(someip_stack),
                    someip_thread, NULL, NULL, NULL, 6, 0, K_NO_WAIT);

    // 5. Main loop: LED heartbeat + watchdog
    //    NORMAL에서만 500ms LED 토글. SAFE 중 LED는 safety가 소유 (100ms 빠른 점멸).
    while (1) {
        if (!safety_is_safe_state()) {
            gpio_pin_toggle_dt(&led);
            k_msleep(500);
        } else {
            k_msleep(100); // SAFE 중에는 LED 토글하지 않고 safety 타이머 콜백에 위임
        }
        // Watchdog은 someip_thread에서 kick, 여기서는 Safety 타이머가 감시
    }
    return 0;
}
