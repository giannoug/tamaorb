// SPDX-License-Identifier: GPL-3.0-or-later
// UI task: touch and BOOT → screen state → emulator buttons; redraws on new frames (spec §5).
#include "ui_task.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tama_core.h"
#include "ui.h"
#include "emu_task.h"

#define LOOP_MS        30
#define SCREEN_TIMEOUT 30000
#define BACKLIGHT_PCT  80

static const char *TAG = "ui";
static tc_model_t s_model;
static uint16_t *s_fb;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void ui_task(void *arg)
{
    ui_screen_t screen;
    ui_screen_init(&screen, now_ms());
    ui_touch_tracker_t tracker = { 0 };
    uint32_t frame_seq = 0;
    uint8_t shown_held = 0xFF;
    bool backlight = false;

    // Before the emulator runs: a start-up screen, then the catch-up progress screen.
    uint32_t last_progress_draw = 0;
    bool boot_drawn = false;
    while (emu_phase() != EMU_RUN) {
        if (emu_phase() == EMU_BOOT && !boot_drawn) {
            ui_render_message(s_fb, "TamaOrb", "Starting...", &UI_PALETTE_CLASSIC);
            board_display_flush(s_fb);
            board_backlight_set(BACKLIGHT_PCT);
            boot_drawn = true;
        } else if (emu_phase() == EMU_CATCHUP && now_ms() - last_progress_draw >= 1000) {
            uint64_t done, total;
            uint32_t eta;
            emu_catchup_progress(&done, &total, &eta);
            ui_render_catchup(s_fb, done, total, eta, &UI_PALETTE_CLASSIC);
            board_display_flush(s_fb);
            board_backlight_set(BACKLIGHT_PCT);
            last_progress_draw = now_ms();
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
    ui_screen_init(&screen, now_ms());
    backlight = false;  // force a fresh pet frame and backlight on

    for (;;) {
        board_touch_t bt;
        board_touch_read(&bt);
        ui_touch_t t = { .count = bt.count };
        for (int i = 0; i < bt.count; i++) { t.pt[i].x = bt.pt[i].x; t.pt[i].y = bt.pt[i].y; }
        tc_tone_t tone;
        tc_get_tone(&tone);

        ui_screen_out_t out = ui_screen_update(&screen, now_ms(), ui_touch_track(&tracker, &t),
                                               board_boot_down(), tone.on, SCREEN_TIMEOUT);
        tc_post_buttons(out.buttons);

        bool redraw = false;
        if (out.backlight != backlight) {
            backlight = out.backlight;
            redraw = backlight;
            if (!backlight) board_backlight_set(0);
        }
        if (backlight && (tc_frame_changed(&frame_seq) || out.buttons != shown_held || redraw)) {
            tc_frame_t f;
            tc_get_frame(&f);
            ui_render_pet(s_fb, &f, s_model, out.buttons, true, &UI_PALETTE_CLASSIC);
            board_display_flush(s_fb);
            shown_held = out.buttons;
            if (redraw) board_backlight_set(BACKLIGHT_PCT);  // light up only after a fresh frame is on the panel
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

void ui_task_start(tc_model_t model)
{
    s_model = model;
    s_fb = heap_caps_malloc(UI_W * UI_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_fb) {
        ESP_LOGE(TAG, "no memory for framebuffer");
        return;
    }
    xTaskCreatePinnedToCore(ui_task, "ui", 6144, NULL, 5, NULL, 0);
}
