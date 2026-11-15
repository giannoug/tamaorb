// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "ui.h"
#include "rom_store.h"

static const char *TAG = "tamaorb";
static uint16_t s_fb[UI_W * UI_H];

void app_main(void)
{
    ESP_LOGI(TAG, "TamaOrb %s", esp_app_get_description()->version);
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(rom_store_init());
    static tc_rom_entry_t roms[8];
    static uint8_t cached[TC_ROM_BYTES];
    uint8_t csha[20];
    int n = rom_store_scan(roms, 8), idx = -1;
    for (int i = 0; i < n; i++) ESP_LOGI(TAG, "SD ROM %s: %s", roms[i].name, tc_model_name(roms[i].model));
    bool have_cached = rom_store_cached(cached, csha);
    tc_sel_action_t sel = tc_rom_select(have_cached, roms, n, &idx);
    ESP_LOGI(TAG, "cached=%d selection=%d (0 cached, 1 sd, 2 picker, 3 none) idx=%d", have_cached, sel, idx);

    // Bring-up (Tasks 9-10): test pattern; held zones tint under the finger;
    // BOOT toggles colour bars (red, green, blue, white, palette bg, palette ink from top to bottom).
    tc_frame_t f = { .icons = 1u << 0 };
    f.rows[0] = 1u;
    f.rows[15] = 1u << 31;
    uint8_t shown = 0xFF;
    bool bars = false, boot_was = false;
    ui_touch_tracker_t tr = { 0 };
    board_backlight_set(80);
    for (;;) {
        board_touch_t bt;
        board_touch_read(&bt);
        ui_touch_t t = { .count = bt.count };
        for (int i = 0; i < bt.count; i++) { t.pt[i].x = bt.pt[i].x; t.pt[i].y = bt.pt[i].y; }
        uint8_t held = ui_touch_track(&tr, &t);
        if (held != shown && !bars) {
            ui_render_pet(s_fb, &f, TC_MODEL_P1, held, true, &UI_PALETTE_CLASSIC);
            board_display_flush(s_fb);
            ESP_LOGI(TAG, "touch n=%d p1=(%d,%d) p2=(%d,%d) held=%c%c%c", bt.count, bt.pt[0].x, bt.pt[0].y,
                     bt.pt[1].x, bt.pt[1].y, held & 1 ? 'A' : '-', held & 2 ? 'B' : '-', held & 4 ? 'C' : '-');
            shown = held;
        }
        bool boot = board_boot_down();
        if (boot && !boot_was) {
            bars = !bars;
            if (bars) {
                const uint16_t c[6] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, ui_rgb565(UI_PALETTE_CLASSIC.bg), ui_rgb565(UI_PALETTE_CLASSIC.ink) };
                for (int y = 0; y < UI_H; y++)
                    for (int x = 0; x < UI_W; x++) s_fb[y * UI_W + x] = c[y / 40];
                board_display_flush(s_fb);
            } else {
                shown = 0xFF;
            }
            ESP_LOGI(TAG, "BOOT: %s", bars ? "colour bars" : "test pattern");
        }
        boot_was = boot;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
