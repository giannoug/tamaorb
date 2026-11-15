// SPDX-License-Identifier: GPL-3.0-or-later
#include "power.h"
#include "board.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";
static esp_pm_lock_handle_t s_screen_lock;
static void (*s_save_now)(void);
static void (*s_show_low)(void);

uint8_t power_battery_pct(uint32_t mv)
{
    // xiaozhi's raw-ADC table converted to cell millivolts with this board's measured scale
    // (raw 2557 ≈ 4170 mV); needs a proper discharge-curve calibration later (spec §9).
    static const struct { uint16_t mv; uint8_t pct; } T[] = {
        { 3230, 0 }, { 3390, 20 }, { 3525, 40 }, { 3670, 60 }, { 3815, 80 }, { 4040, 100 },
    };
    if (mv <= T[0].mv) return 0;
    for (int i = 1; i < 6; i++)
        if (mv < T[i].mv) return (uint8_t)(T[i - 1].pct + (mv - T[i - 1].mv) * (T[i].pct - T[i - 1].pct) / (T[i].mv - T[i - 1].mv));
    return 100;
}

static void deep_sleep(void)
{
    esp_sleep_enable_timer_wakeup((uint64_t)POWER_WAKE_CHECK_S * 1000000ULL);
    esp_deep_sleep_start();
}

void power_early_boot_check(void)
{
    if ((esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER)) && !board_usb_present()) deep_sleep();
}

static void monitor(void *arg)
{
    int low = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        uint32_t mv = board_battery_mv();  // 0 = unknown (ADC unavailable): never treated as empty
        bool usb = board_usb_present();
        low = (!usb && mv && mv < POWER_LOW_MV) ? low + 1 : 0;
        if (low < POWER_LOW_READS) continue;

        ESP_LOGW(TAG, "battery critical (%lu mV): saving and sleeping", (unsigned long)mv);
        if (s_save_now) s_save_now();
        if (s_show_low) s_show_low();
        vTaskDelay(pdMS_TO_TICKS(3000));
        board_backlight_set(0);
        board_volume_set(0);
        esp_wifi_stop();
        deep_sleep();
    }
}

void power_screen_on(bool on)
{
    // esp_pm locks are counted; track our single hold so repeated calls can't leak a count
    // (a leaked count kept light sleep off until reboot).
    static bool held;
    if (!s_screen_lock || on == held) return;
    if (on) esp_pm_lock_acquire(s_screen_lock);
    else esp_pm_lock_release(s_screen_lock);
    held = on;
}

void power_start(void (*save_now)(void), void (*show_low)(void))
{
    s_save_now = save_now;
    s_show_low = show_low;
    esp_pm_config_t pm = { .max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true };
    esp_err_t err = esp_pm_configure(&pm);
    if (err == ESP_OK) err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "screen", &s_screen_lock);
    if (err != ESP_OK) ESP_LOGW(TAG, "power management unavailable: %s", esp_err_to_name(err));
    xTaskCreatePinnedToCore(monitor, "power", 3072, NULL, 3, NULL, 0);
}
