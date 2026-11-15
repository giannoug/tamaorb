// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "board.h"
#include "ui.h"

static const char *TAG = "tamaorb";
static uint16_t s_fb[UI_W * UI_H];

void app_main(void)
{
    ESP_LOGI(TAG, "TamaOrb %s", esp_app_get_description()->version);
    ESP_ERROR_CHECK(board_init());

    // Bring-up test pattern (Task 9): pixels (0,0) and (31,15) and the Feed icon lit.
    tc_frame_t f = { .icons = 1u << 0 };
    f.rows[0] = 1u;
    f.rows[15] = 1u << 31;
    ui_render_pet(s_fb, &f, TC_MODEL_P1, 0, true, &UI_PALETTE_CLASSIC);
    board_display_flush(s_fb);
    board_backlight_set(80);
    ESP_LOGI(TAG, "test pattern shown");
}
