// SPDX-License-Identifier: GPL-3.0-or-later
// The Web panel QR, encoded by Espressif's registry component and handed to ui as a module grid.
#include "qr_encode.h"
#include <string.h>
#include "esp_log.h"
#include "qrcode.h"

static void copy_modules(esp_qrcode_handle_t q, void *user_data)
{
    ui_qr_t *out = user_data;
    int size = esp_qrcode_get_size(q);
    if (size <= 0 || size > UI_QR_MAX) return;  // out->size stays 0: not drawn
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) out->mod[y][x] = esp_qrcode_get_module(q, x, y);
    out->size = (uint8_t)size;
}

bool qr_encode(const char *text, ui_qr_t *out)
{
    memset(out, 0, sizeof *out);
    esp_log_level_set("QRCODE", ESP_LOG_WARN);  // the component logs the text it encodes: here, the ball's IP
    esp_qrcode_config_t cfg = {
        .display_func_with_cb = copy_modules,
        .max_qrcode_version = 4,  // 33 x 33 = UI_QR_MAX
        .qrcode_ecc_level = ESP_QRCODE_ECC_MED,
        .user_data = out,  // non-NULL: the component calls display_func_with_cb
    };
    return esp_qrcode_generate(&cfg, text) == ESP_OK && out->size > 0;
}
