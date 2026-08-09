#pragma once
// ISO 26262 Safe State + Watchdog (ASIL-B)
#include <stdbool.h>
#include <zephyr/drivers/gpio.h>

void safety_init(const struct device *can_dev, const struct gpio_dt_spec *led);
void safety_kick_watchdog(void);      // heartbeat 수신 시 호출: WDT feed + 타이머 리셋 + SAFE->NORMAL 복귀
bool safety_is_safe_state(void);
