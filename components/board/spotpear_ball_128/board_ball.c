// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"
#include "ball_internal.h"
#include "ball_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board";

static void backlight_init(void)
{
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                              .timer_num = LEDC_TIMER_0, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t c = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
                                .timer_sel = LEDC_TIMER_0, .duty = 0, .flags.output_invert = 1 };  // active low
    ledc_channel_config(&c);
}

void board_backlight_set(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (pct == 0) {
        // Off: stop the PWM with the pin high (backlight is active low) and latch the pad, so it
        // can't float during automatic light sleep, which isolates GPIOs by default.
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);  // LEDC idle 0 -> inverted -> pin high
        gpio_hold_en(PIN_LCD_BL);
        return;
    }
    gpio_hold_dis(PIN_LCD_BL);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (1023u * pct) / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t board_init(void)
{
    gpio_set_direction(PIN_POWER_HOLD, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_POWER_HOLD, 1);
    gpio_set_direction(PIN_AMP_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_AMP_EN, 0);
    backlight_init();
    if (ball_power_init() != ESP_OK) ESP_LOGW(TAG, "battery ADC unavailable; low-battery handling is off");
    gpio_config_t boot = { .pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&boot);
    ESP_RETURN_ON_ERROR(ball_display_init(), TAG, "display");
    if (ball_touch_init() != ESP_OK) ESP_LOGW(TAG, "touch init failed; continuing without touch");
    if (ball_audio_init() != ESP_OK) ESP_LOGW(TAG, "audio init failed; continuing without sound");
    return ESP_OK;
}
