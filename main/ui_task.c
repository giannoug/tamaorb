// SPDX-License-Identifier: GPL-3.0-or-later
// UI task: touch and BOOT → screen state → emulator buttons; redraws on new frames.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rom_store.h"
#include "settings_store.h"
#include "power.h"
#include "press_task.h"
#include "rom_switch.h"

#define LOOP_MS        30

static const char *TAG = "ui";
static tc_model_t s_model;
static uint16_t *s_fb;
static volatile bool s_low_battery;

void ui_task_show_low_battery(void) { s_low_battery = true; }

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void picker_from_play(void);
static uint8_t read_touch_buttons(ui_touch_tracker_t *tr);

static void ui_task(void *arg)
{
    ui_screen_t screen;
    ui_screen_init(&screen, now_ms());
    ui_touch_tracker_t tracker = { 0 };
    uint32_t frame_seq = 0;
    uint8_t shown_held = 0xFF;
    bool backlight = false;
    bool usb_was = true;  // no wake event for the USB state at boot
    bool dimmed = false;
    uint8_t shown_level = 0;

    // Before the emulator runs: a start-up screen, then the catch-up progress screen.
    uint32_t last_progress_draw = 0;
    bool boot_drawn = false;
    st_settings_t cfg;
    settings_get(&cfg);
    power_screen_on(true);  // these screens are lit: no light sleep (it would stop the backlight PWM)
    while (emu_phase() != EMU_RUN) {
        settings_get(&cfg);
        const ui_palette_t *pal = ui_palette_by_id(cfg.palette);
        if (s_low_battery) {
            ui_render_low_battery(s_fb, pal);
            board_display_flush(s_fb);
            board_backlight_set(cfg.brightness);
            vTaskSuspend(NULL);  // the power task puts the device to sleep
        }
        if (emu_phase() == EMU_BOOT && !boot_drawn) {
            ui_render_message(s_fb, "TamaOrb", "Starting...", pal);
            board_display_flush(s_fb);
            board_backlight_set(cfg.brightness);
            boot_drawn = true;
        } else if (emu_phase() == EMU_CATCHUP && now_ms() - last_progress_draw >= 1000) {
            uint64_t done, total;
            uint32_t eta;
            emu_catchup_progress(&done, &total, &eta);
            ui_render_catchup(s_fb, done, total, eta, pal);
            board_display_flush(s_fb);
            ESP_LOGI(TAG, "catch-up screen %llu/%llu s", (unsigned long long)done, (unsigned long long)total);
            board_backlight_set(cfg.brightness);
            last_progress_draw = now_ms();
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
    {
        ui_touch_tracker_t probe = { 0 };  // a finger held through the catch-up screen isn't a press
        ui_screen_resume(&screen, now_ms(), board_boot_down(), read_touch_buttons(&probe) != 0);
    }
    power_screen_on(false);
    backlight = false;  // force a fresh pet frame and backlight on
    ESP_LOGI(TAG, "showing the pet");

    uint32_t disp_seq = settings_display_seq();
    uint8_t volume = cfg.volume;
    int pal_id = cfg.palette;
    for (;;) {
        settings_get(&cfg);
        if (settings_display_seq() != disp_seq) {
            disp_seq = settings_display_seq();
            ui_screen_wake(&screen, now_ms());
        }
        if (cfg.volume != volume) { volume = cfg.volume; board_volume_set(volume); }
        const ui_palette_t *pal = ui_palette_by_id(cfg.palette);
        bool pal_changed = (int)cfg.palette != pal_id;
        pal_id = cfg.palette;
        uint8_t dim_pct = st_effective_dim(&cfg);
        board_touch_t bt;
        board_touch_read(&bt);
        ui_touch_t t = { .count = bt.count };
        for (int i = 0; i < bt.count; i++) { t.pt[i].x = bt.pt[i].x; t.pt[i].y = bt.pt[i].y; }
        tc_tone_t tone;
        tc_get_tone(&tone);

        // Plugged in: no automatic timeout, and plugging in wakes the screen. On battery: 30 s.
        bool usb = board_usb_present();
        if (usb && !usb_was) ui_screen_wake(&screen, now_ms());
        usb_was = usb;
        // Agent presses go through tc_post_remote_buttons, never the touch path: they aren't activity.
        // Their beeps wake the screen only with agent_feedback = visible.
        bool sound_quiet = press_window_active() && cfg.agent_feedback != ST_AGENT_VISIBLE;
        ui_screen_out_t out = ui_screen_update(&screen, now_ms(), ui_touch_track(&tracker, &t),
                                               board_boot_down(), tone.on, sound_quiet, usb ? 0 : cfg.off_battery_s * 1000u);
        tc_post_buttons(out.buttons);
        if (ui_screen_long_press(&screen)) {
            tc_post_buttons(0);
            picker_from_play();
            ui_touch_tracker_t probe = { 0 };
            ui_screen_resume(&screen, now_ms(), board_boot_down(), read_touch_buttons(&probe) != 0);
            power_screen_on(false);
            backlight = false;  // force a fresh pet frame
            continue;
        }

        if (s_low_battery) {
            ui_render_low_battery(s_fb, pal);
            board_display_flush(s_fb);
            board_backlight_set(cfg.brightness);
            power_screen_on(true);
            vTaskSuspend(NULL);  // the power task puts the device to sleep
        }

        bool redraw = false;
        if (out.backlight != backlight) {
            backlight = out.backlight;
            redraw = backlight;
            power_screen_on(backlight);  // no light sleep while the backlight PWM must run
            if (!backlight) board_backlight_set(0);
        }
        uint32_t dim_s = usb ? cfg.dim_usb_s : cfg.dim_battery_s;
        bool dim = ui_screen_dimmed(&screen, now_ms(), dim_s ? dim_s * 1000u : UINT32_MAX);
        uint8_t level = dim ? dim_pct : cfg.brightness;
        if (backlight && !redraw && (dim != dimmed || level != shown_level)) board_backlight_set(level);
        dimmed = dim;
        if (pal_changed) redraw = true;
        if (backlight && (tc_frame_changed(&frame_seq) || out.buttons != shown_held || redraw)) {
            tc_frame_t f;
            tc_get_frame(&f);
            ui_render_pet(s_fb, &f, s_model, out.buttons, true, pal);
            board_display_flush(s_fb);
            shown_held = out.buttons;
            if (redraw) board_backlight_set(level);  // light up only after a fresh frame
        }
        if (backlight) shown_level = level;
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

esp_err_t ui_init(void)
{
    // PSRAM: internal RAM can't spare 115 KB once Wi-Fi runs (measured). The display driver
    // copies each frame into small internal DMA bands, so the framebuffer itself needn't be internal.
    s_fb = heap_caps_malloc(UI_W * UI_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_fb ? ESP_OK : ESP_ERR_NO_MEM;
}

void ui_message(const char *title, const char *body)
{
    if (!s_fb) return;
    st_settings_t cfg;
    settings_get(&cfg);
    ui_render_message(s_fb, title, body, ui_palette_by_id(cfg.palette));
    board_display_flush(s_fb);
    board_backlight_set(cfg.brightness);
}

static uint8_t read_touch_buttons(ui_touch_tracker_t *tr)
{
    board_touch_t bt;
    board_touch_read(&bt);
    ui_touch_t t = { .count = bt.count };
    for (int i = 0; i < bt.count; i++) { t.pt[i].x = bt.pt[i].x; t.pt[i].y = bt.pt[i].y; }
    return ui_touch_track(tr, &t);
}

int ui_pick_rom(const tc_rom_entry_t *roms, int n, int current, bool allow_cancel)
{
    int idx[16], sel = 0;
    int m = tc_rom_distinct_valid(roms, n, idx, 16);  // one entry per distinct P1/P2 dump
    for (int k = 0; k < m; k++)
        if (current >= 0 && !memcmp(roms[idx[k]].sha1, roms[current].sha1, 20)) sel = k;
    if (!m || !s_fb) return -1;

    static char subs[UI_PICKER_MAX][24];
    ui_touch_tracker_t tr = { 0 };
    uint8_t prev = 0xFF;  // ignore whatever is held when the picker opens
    bool boot_prev = true, dirty = true;
    for (;;) {
        if (dirty) {
            int first = sel - 1 < 0 ? 0 : (sel - 1 > m - UI_PICKER_MAX ? (m > UI_PICKER_MAX ? m - UI_PICKER_MAX : 0) : sel - 1);
            const char *names[UI_PICKER_MAX], *sp[UI_PICKER_MAX];
            int shown = m - first < UI_PICKER_MAX ? m - first : UI_PICKER_MAX;
            for (int k = 0; k < shown; k++) {
                const tc_rom_entry_t *r = &roms[idx[first + k]];
                names[k] = tc_model_name(r->model);
                snprintf(subs[k], sizeof subs[k], "Gen %d - %.12s", r->model == TC_MODEL_P1 ? 1 : 2, r->name);
                sp[k] = subs[k];
            }
            st_settings_t cfg;
            settings_get(&cfg);
            ui_render_picker(s_fb, names, sp, shown, sel - first, ui_palette_by_id(cfg.palette));
            board_display_flush(s_fb);
            board_backlight_set(cfg.brightness);
            dirty = false;
        }
        uint8_t b = read_touch_buttons(&tr);
        uint8_t pressed = prev == 0xFF ? 0 : (uint8_t)(b & ~prev);
        prev = b;
        if (pressed & TC_BTN_MASK(TC_BTN_A)) { sel = (sel + m - 1) % m; dirty = true; }
        if (pressed & TC_BTN_MASK(TC_BTN_C)) { sel = (sel + 1) % m; dirty = true; }
        if (pressed & TC_BTN_MASK(TC_BTN_B)) return idx[sel];
        bool boot = board_boot_down();
        if (allow_cancel && boot && !boot_prev) return -1;
        boot_prev = boot;
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

// BOOT long press during play: pick a ROM; a different one is saved-then-rebooted into.
// The switch itself is shared with the remote API (rom_switch_to); the messages stay here.
static void picker_from_play(void)
{
    static tc_rom_entry_t roms[16];
    uint8_t active[20];
    int n = rom_store_scan(roms, 16), current = -1;
    uint8_t *rom = malloc(TC_ROM_BYTES);  // only to learn the active SHA-1
    if (rom && rom_store_cached(rom, active))
        for (int i = 0; i < n; i++)
            if (!memcmp(roms[i].sha1, active, 20)) current = i;
    free(rom);
    int pick = ui_pick_rom(roms, n, current, true);
    if (pick < 0 || pick == current) return;
    ui_message("Switching", "Saving your pet...");
    esp_err_t err = rom_switch_to(roms[pick].name);
    if (err == ESP_OK) {
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));  // keep this message up until the restart
    }
    if (err == ESP_FAIL) {
        ui_message("Not switching", "Couldn't save your\npet, so it stays.");
        vTaskDelay(pdMS_TO_TICKS(2500));
        return;
    }
    ui_message("Switch failed", "Could not read\nthat ROM.");
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void ui_task_start(tc_model_t model)
{
    s_model = model;
    if (!s_fb) {
        ESP_LOGE(TAG, "ui_init() not called");
        return;
    }
    xTaskCreatePinnedToCore(ui_task, "ui", 6144, NULL, 5, NULL, 0);
}
