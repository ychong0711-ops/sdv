#include "safety.h"
#include "can_gateway.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(safety, LOG_LEVEL_INF);

#define WDT_NODE DT_ALIAS(watchdog0)
static const struct device *wdt = DEVICE_DT_GET(WDT_NODE);
static struct k_timer safety_timer;

#define HEARTBEAT_TIMEOUT_MS 100
#define WDT_TIMEOUT_MS 150

// 상태 머신: NORMAL / SAFE
static bool safe_state = false; // false = NORMAL, true = SAFE
static const struct device *safety_can_dev = NULL;
static const struct gpio_dt_spec *safety_led = NULL;

// ISO 26262 Safe State 진입: SAFE 전환 + CAN 경고 1회 + LED 토글 시작
static void enter_safe_state(void) {
    if (safe_state) return; // 이미 SAFE면 재진입 없음
    LOG_WRN("=== SAFE STATE ENTERED ===");
    safe_state = true;
    if (safety_can_dev) can_send_warning(safety_can_dev); // CAN 경고 프레임 (level=0xFF)
    if (safety_led) gpio_pin_toggle_dt(safety_led);       // LED 빠른 점멸 시작
}

static void safety_timeout(struct k_timer *timer) {
    ARG_UNUSED(timer);
    if (!safe_state) {
        // NORMAL: MPU heartbeat 타임아웃 -> SAFE 진입
        enter_safe_state();
    } else {
        // SAFE: 안전 동작 반복 (CAN 경고 + LED 토글 + WDT feed - 의도적 유지, 리셋 금지)
        // 주의: 타이머 콜백은 system workqueue 컨텍스트 -> k_sleep/k_msleep 금지.
        // can_send는 K_MSEC(5) 이하 타임아웃 사용.
        if (safety_can_dev) can_send_warning(safety_can_dev);
        if (safety_led) gpio_pin_toggle_dt(safety_led);
        if (device_is_ready(wdt)) wdt_feed(wdt, 0);
    }
}

void safety_init(const struct device *can_dev, const struct gpio_dt_spec *led) {
    safety_can_dev = can_dev;
    safety_led = led;

    // 1. Hardware Watchdog (STM32 IWDG)
    if (device_is_ready(wdt)) {
        struct wdt_timeout_cfg cfg = {
            .window = {0, WDT_TIMEOUT_MS},
            .callback = nullptr,
            .flags = WDT_FLAG_RESET_SOC
        };
        wdt_install_timeout(wdt, &cfg);
        wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
        LOG_INF("Watchdog: %dms (IWDG)", WDT_TIMEOUT_MS);
    } else {
        LOG_WRN("Watchdog not ready, using software timer only");
    }

    // 2. Safety Timer (MPU heartbeat monitor)
    k_timer_init(&safety_timer, safety_timeout, nullptr);
    k_timer_start(&safety_timer, K_MSEC(HEARTBEAT_TIMEOUT_MS), K_MSEC(HEARTBEAT_TIMEOUT_MS));
    LOG_INF("Safety timer: %dms heartbeat timeout", HEARTBEAT_TIMEOUT_MS);
}

void safety_kick_watchdog(void) {
    if (device_is_ready(wdt)) wdt_feed(wdt, 0);
    k_timer_start(&safety_timer, K_MSEC(HEARTBEAT_TIMEOUT_MS), K_MSEC(HEARTBEAT_TIMEOUT_MS));
    // heartbeat 복구 시 SAFE -> NORMAL 복귀
    if (safe_state) {
        LOG_INF("Heartbeat recovered -> NORMAL state");
        safe_state = false;
    }
}

bool safety_is_safe_state(void) {
    return safe_state;
}
