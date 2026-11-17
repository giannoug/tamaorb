// SPDX-License-Identifier: GPL-3.0-or-later
#include "power.h"
#include "board.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "power";
static esp_pm_lock_handle_t s_screen_lock;
static esp_pm_lock_handle_t s_awake_lock;  // power_keep_awake's hold, separate from the screen's
static esp_timer_handle_t s_awake_timer;
static SemaphoreHandle_t s_awake_mutex;
static bool s_awake_held;
static int64_t s_awake_until;  // µs (esp_timer clock): the hold's end, moved out by every power_keep_awake
static void (*s_awake_hook)(bool awake);
static void (*s_save_now)(void);
static void (*s_show_low)(void);

uint8_t power_battery_pct(uint32_t mv)
{
    // xiaozhi's raw-ADC table converted to cell millivolts with this board's measured scale
    // (raw 2557 ≈ 4170 mV); needs a proper discharge-curve calibration later.
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

static void awake_end(void *arg)
{
    xSemaphoreTake(s_awake_mutex, portMAX_DELAY);
    // A call that came in while this callback waited for the mutex moved the end out and re-armed the timer.
    bool was = s_awake_held && esp_timer_get_time() >= s_awake_until - 1000;
    if (was) {
        esp_pm_lock_release(s_awake_lock);
        s_awake_held = false;
    }
    xSemaphoreGive(s_awake_mutex);
    if (was && s_awake_hook) s_awake_hook(false);
    if (was) ESP_LOGI(TAG, "keep-awake ended");
}

void power_set_awake_hook(void (*hook)(bool awake)) { s_awake_hook = hook; }

void power_keep_awake(uint32_t ms)
{
    if (!s_awake_lock || !s_awake_timer) return;  // before power_start
    xSemaphoreTake(s_awake_mutex, portMAX_DELAY);
    bool started = !s_awake_held;
    if (started) {
        esp_pm_lock_acquire(s_awake_lock);
        s_awake_held = true;
    }
    s_awake_until = esp_timer_get_time() + (int64_t)ms * 1000;
    esp_timer_stop(s_awake_timer);  // not running is fine
    esp_timer_start_once(s_awake_timer, (uint64_t)ms * 1000);
    xSemaphoreGive(s_awake_mutex);
    if (started && s_awake_hook) s_awake_hook(true);
    if (started) ESP_LOGI(TAG, "keep-awake started");
}

void power_start(void (*save_now)(void), void (*show_low)(void))
{
    s_save_now = save_now;
    s_show_low = show_low;
    esp_pm_config_t pm = { .max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true };
    esp_err_t err = esp_pm_configure(&pm);
    if (err == ESP_OK) err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "screen", &s_screen_lock);
    s_awake_mutex = xSemaphoreCreateMutex();
    const esp_timer_create_args_t t = { .callback = awake_end, .name = "awake" };
    if (err == ESP_OK && s_awake_mutex && esp_timer_create(&t, &s_awake_timer) == ESP_OK)
        err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "awake", &s_awake_lock);
    if (err != ESP_OK) ESP_LOGW(TAG, "power management unavailable: %s", esp_err_to_name(err));
    xTaskCreatePinnedToCore(monitor, "power", 3072, NULL, 3, NULL, 0);
}
