// SPDX-License-Identifier: GPL-3.0-or-later
#include "esp_log.h"
#include "esp_app_desc.h"
#include "tamalib.h"

static const char *TAG = "tamaorb";

void app_main(void)
{
    ESP_LOGI(TAG, "TamaOrb %s", esp_app_get_description()->version);
    tamalib_register_hal(NULL);  // links tamalib; replaced by the real boot sequence later
}
