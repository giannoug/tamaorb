// SPDX-License-Identifier: GPL-3.0-or-later
#include "factory_reset.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "net.h"
#include "nvs_flash.h"
#include "rom_store.h"
#include "rom_switch.h"
#include "saver.h"

static const char *TAG = "reset";
#define RESET_MAGIC 0xFAC70E5Eu
static RTC_NOINIT_ATTR uint32_t s_reset_marker;  // survives the software restart; random after a power-on

esp_err_t factory_reset_request(void)
{
    if (app_restart_pending()) return ESP_ERR_NOT_ALLOWED;
    saver_freeze();  // nothing may land in NVS after this: the next boot erases it
    s_reset_marker = RESET_MAGIC;
    ESP_LOGW(TAG, "factory reset requested; erasing at the restart");
    app_restart_after(500);
    return ESP_OK;
}

bool factory_reset_boot(void)
{
    if (s_reset_marker != RESET_MAGIC) return false;
    ESP_LOGW(TAG, "erasing every pet, setting and Wi-Fi");
    esp_err_t a = nvs_flash_erase_partition(ROM_STORE_PARTITION);
    esp_err_t b = nvs_flash_erase_partition(NVS_DEFAULT_PART_NAME);
    net_forget_time();
    if (a == ESP_OK && b == ESP_OK) s_reset_marker = 0;
    else ESP_LOGE(TAG, "erase failed (%s, %s); trying again at the next boot", esp_err_to_name(a), esp_err_to_name(b));
    return true;
}
