// SPDX-License-Identifier: GPL-3.0-or-later
// UI task: touch and BOOT → screen state → emulator buttons or the glance and settings screens; redraws.
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
#include <time.h>
#include "net.h"
#include "rom_store.h"
#include "settings_store.h"
#include "power.h"
#include "press_task.h"
#include "rom_switch.h"
#include "qr_encode.h"
#include "pet_clock.h"

#define LOOP_MS        30
#define FRAME_MS       16   // while a finger is on the glance or settings: ~60 frames a second

static const char *TAG = "ui";
static tc_model_t s_model;
static uint16_t *s_fb;
static ui_sent_t s_sent;  // row hashes of what the panel shows (internal RAM, 1.9 KB), so a frame sends only the rows that changed
static volatile bool s_low_battery;

void ui_task_show_low_battery(void) { s_low_battery = true; }

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// The screen lit or dark, and with it: light sleep held off (the backlight PWM needs the clock), the emulator's
// sleep stretch (2 ms lit; 20 ms dark, so the ball can light-sleep: it needs >= 8 ms idle) and the speaker
// (dark: off between sounds). One place, so the three never disagree.
#define DARK_STRETCH_US 20000
static void screen_lit(bool lit)
{
    power_screen_on(lit);
    tc_set_sleep_stretch_us(lit ? TC_SLEEP_STRETCH_US_DEFAULT : DARK_STRETCH_US);
    board_audio_idle_off(!lit);
}

static void send_rows(void *ctx, const uint16_t *fb, int y0, int y1) { board_display_flush_rows(fb, y0, y1); }
static void show(void) { ui_send_changed(&s_sent, s_fb, send_rows, NULL); }  // every frame goes through here

static void read_touch(ui_touch_t *t);
static void load_rom_rows(ui_menu_view_t *v);
static void switch_rom(int row, int rows);
static void render_glance(const ui_palette_t *pal, int8_t batt_pct);
static void resume_on_pet(ui_screen_t *screen, ui_gesture_t *gest, ui_nav_t *nav);

// The Pet clock row and pages: the view's clock fields, refreshed every loop while the menu shows.
static uint32_t s_clock_id;
static bool s_clock_pending;

static ui_clock_why_t clock_why(void)
{
    if (s_clock_pending) return UI_CLOCK_PENDING;
    switch (pet_clock_available()) {
    case PET_CLOCK_OK: return UI_CLOCK_OK;
    case PET_CLOCK_UNSUPPORTED: return UI_CLOCK_UNSUPPORTED;
    case PET_CLOCK_NO_TIME: return UI_CLOCK_NO_TIME;
    default: return UI_CLOCK_CATCHUP;
    }
}

// Every loop, whatever shows: the poll that sees the request applied also asks for the save.
static void clock_poll(ui_menu_view_t *v)
{
    if (!s_clock_pending) return;
    pet_clock_status_t r = pet_clock_poll(s_clock_id, NULL);
    if (r == PET_CLOCK_PENDING) return;
    s_clock_pending = false;
    v->clock_busy = r == PET_CLOCK_BUSY;
    ESP_LOGI(TAG, "pet clock: %s", r == PET_CLOCK_OK ? "set" : "busy");
}

// Fills the view's clock fields; true when what the list row or the Pet clock page shows has changed.
static bool clock_refresh(ui_menu_view_t *v)
{
    if (v->page != UI_PAGE_CLOCK && v->page != UI_PAGE_CLOCK_CONFIRM) v->clock_busy = false;  // left the page
    int32_t pet = pet_clock_pet_s(), ball = pet_clock_ball_s();
    ui_clock_why_t why = clock_why();
    bool changed = why != v->clock_why || (v->page == UI_PAGE_CLOCK ? pet != v->pet_s || ball != v->ball_s
                                                                     : pet / 60 != v->pet_s / 60);
    v->pet_s = pet;
    v->ball_s = ball;
    v->clock_why = why;
    return changed;
}

static void ui_task(void *arg)
{
    ui_screen_t screen;
    ui_screen_init(&screen, now_ms());
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
    screen_lit(true);  // these screens are lit: no light sleep (it would stop the backlight PWM)
    while (emu_phase() != EMU_RUN) {
        settings_get(&cfg);
        const ui_palette_t *pal = ui_palette_by_id(cfg.palette);
        if (s_low_battery) {
            ui_render_low_battery(s_fb, pal);
            show();
            board_backlight_set(cfg.brightness);
            vTaskSuspend(NULL);  // the power task puts the device to sleep
        }
        if (emu_phase() == EMU_BOOT && !boot_drawn) {
            ui_render_message(s_fb, "TamaOrb", "Starting...", pal);
            show();
            board_backlight_set(cfg.brightness);
            boot_drawn = true;
        } else if (emu_phase() == EMU_CATCHUP && now_ms() - last_progress_draw >= 1000) {
            uint64_t done, total;
            uint32_t eta;
            emu_catchup_progress(&done, &total, &eta);
            ui_render_catchup(s_fb, done, total, eta, pal);
            show();
            ESP_LOGI(TAG, "catch-up screen %llu/%llu s", (unsigned long long)done, (unsigned long long)total);
            board_backlight_set(cfg.brightness);
            last_progress_draw = now_ms();
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
    ui_gesture_t gest = { 0 };
    ui_nav_t nav;
    ui_nav_init(&nav);
    resume_on_pet(&screen, &gest, &nav);
    screen_lit(false);
    backlight = false;  // force a fresh pet frame and backlight on
    ESP_LOGI(TAG, "showing the pet");

    // The QR page: mDNS answers to the boot name until a restart.
    st_settings_t boot;
    settings_get_boot(&boot);
    char panel_name[40], panel_url[32] = "", ip[16];
    snprintf(panel_name, sizeof panel_name, "%s.local", boot.name);

    uint32_t disp_seq = settings_display_seq(), seq = settings_seq();
    uint8_t volume = st_effective_volume(&cfg);
    int pal_id = cfg.palette;
    uint32_t glance_ms = 0;
    int8_t batt_pct = -1;
    for (;;) {
        uint32_t loop_t0 = now_ms();
        // cfg is updated in place: nav.view.cfg points at it between ui_nav_step calls.
        settings_get(&cfg);
        if (settings_display_seq() != disp_seq) {
            disp_seq = settings_display_seq();
            ui_screen_wake(&screen, now_ms());
        }
        bool cfg_changed = settings_seq() != seq;
        seq = settings_seq();
        if (st_effective_volume(&cfg) != volume) { volume = st_effective_volume(&cfg); board_volume_set(volume); }
        const ui_palette_t *pal = ui_palette_by_id(cfg.palette);
        bool pal_changed = (int)cfg.palette != pal_id;
        pal_id = cfg.palette;
        uint8_t dim_pct = st_effective_dim(&cfg);
        ui_touch_t t;
        read_touch(&t);
        tc_tone_t tone;
        tc_get_tone(&tone);

        // The pet screen takes pet taps; the glance and settings take menu gestures. Edge swipes work on all.
        ui_gmode_t mode = nav.screen == UI_SCREEN_PET ? UI_GMODE_PET : UI_GMODE_MENU;
        ui_gevent_t ev;
        uint8_t touch = ui_gesture_feed(&gest, mode, &t, now_ms(), &ev);
        if (t.count) touch |= UI_TOUCH_PRESENT;  // swipes, menu taps and drags are activity too

        // Plugged in: no automatic timeout, and plugging in wakes the screen. On battery: 30 s.
        bool usb = board_usb_present();
        if (usb && !usb_was) ui_screen_wake(&screen, now_ms());
        usb_was = usb;
        // Agent presses go through tc_post_remote_buttons, never the touch path: they aren't activity.
        // Their beeps wake the screen only with agent_feedback = visible.
        bool sound_quiet = press_window_active() && cfg.agent_feedback != ST_AGENT_VISIBLE;
        screen.boot_nav = nav.screen != UI_SCREEN_PET;  // there a short BOOT press goes back to the pet
        ui_screen_out_t out = ui_screen_update(&screen, now_ms(), touch,
                                               board_boot_down(), tone.on, sound_quiet, usb ? 0 : cfg.off_battery_s * 1000u);
        tc_post_buttons(mode == UI_GMODE_PET ? out.buttons : 0);
        bool boot_short = ui_screen_short_press(&screen);
        if (screen.swallowing) {  // the touch that woke the screen reaches no screen either
            ui_gesture_reset(&gest);
            ev.kind = UI_G_NONE;
        }

        ui_act_t act = UI_ACT_NONE;
        clock_poll(&nav.view);
        bool was_menu = nav.screen == UI_SCREEN_MENU;
        bool clock_changed = was_menu && clock_refresh(&nav.view);  // before the step: the confirm asks about this ball time
        if (out.backlight) {
            st_settings_t edited;
            bool display_changed;
            nav.view.t_ms = now_ms();  // the confirm page's guard counts from here
            act = ui_nav_step(&nav, &ev, boot_short, &cfg, &edited, &display_changed);
            if (act == UI_ACT_SETTINGS) {
                settings_put(&edited, display_changed);
                settings_get(&cfg);  // the page shows the new value now, not next loop
                seq = settings_seq();
                if (st_effective_volume(&cfg) != volume) { volume = st_effective_volume(&cfg); board_volume_set(volume); }
                pal_changed |= (int)cfg.palette != pal_id;
                pal = ui_palette_by_id(cfg.palette);
                pal_id = cfg.palette;
            }
        } else if (nav.screen != UI_SCREEN_PET) {
            ui_nav_to_pet(&nav);  // switched off or timed out: it wakes on the pet
            ui_gesture_reset(&gest);
        }

        if (!was_menu && nav.screen == UI_SCREEN_MENU) clock_refresh(&nav.view);  // just opened: no stale clock in its first frame
        if (ui_screen_long_press(&screen)) {  // from any screen, lit or not: the ROM list
            tc_post_buttons(0);  // ui_screen_update already woke the screen when the long press fired
            ui_nav_open_roms(&nav, true);
            ui_gesture_reset(&gest);  // a touch held now belongs to no row
            act = UI_ACT_REDRAW;
        }
        if (nav.screen == UI_SCREEN_MENU && nav.view.page == UI_PAGE_ROM && !nav.view.roms) {
            load_rom_rows(&nav.view);  // the SD scan, once per opening
            act = UI_ACT_REDRAW;
        }
        if (act == UI_ACT_CLOCK_SYNC) {
            nav.view.clock_busy = false;
            pet_clock_status_t st = pet_clock_start(&s_clock_id);  // re-checked now: time or the phase may have gone
            s_clock_pending = st == PET_CLOCK_OK;
            if (!s_clock_pending) ESP_LOGW(TAG, "pet clock not set (%d)", (int)st);
            nav.view.clock_why = clock_why();
            act = UI_ACT_REDRAW;
        }
        if (act == UI_ACT_ROM_SWITCH) {
            tc_post_buttons(0);
            switch_rom(nav.view.rom_pick, nav.view.rom_count);  // restarts the ball, or returns after saying why it didn't
            resume_on_pet(&screen, &gest, &nav);
            screen_lit(false);
            backlight = false;  // force a fresh pet frame
            continue;
        }

        if (s_low_battery) {
            ui_render_low_battery(s_fb, pal);
            show();
            board_backlight_set(cfg.brightness);
            screen_lit(true);
            vTaskSuspend(NULL);  // the power task puts the device to sleep
        }

        bool redraw = false;
        if (out.backlight != backlight) {
            backlight = out.backlight;
            redraw = backlight;
            screen_lit(backlight);  // no light sleep while the backlight PWM must run
            if (!backlight) board_backlight_set(0);
        }
        uint32_t dim_s = usb ? cfg.dim_usb_s : cfg.dim_battery_s;
        bool dim = ui_screen_dimmed(&screen, now_ms(), dim_s ? dim_s * 1000u : UINT32_MAX);
        uint8_t level = dim ? dim_pct : cfg.brightness;
        if (backlight && !redraw && (dim != dimmed || level != shown_level)) board_backlight_set(level);
        dimmed = dim;
        bool dirty = redraw || pal_changed || act == UI_ACT_REDRAW || act == UI_ACT_SETTINGS || clock_changed;
        bool frame_new = tc_frame_changed(&frame_seq);
        bool drawn = false;
        if (!backlight) {
            // nothing to draw
        } else if (nav.screen == UI_SCREEN_PET) {
            if (frame_new || out.buttons != shown_held || dirty) {
                tc_frame_t f;
                tc_get_frame(&f);
                ui_render_pet(s_fb, &f, s_model, out.buttons, pal);
                show();
                shown_held = out.buttons;
                drawn = true;
            }
        } else if (nav.screen == UI_SCREEN_GLANCE) {
            bool tick = now_ms() - glance_ms >= 1000;  // the clock and battery
            if (frame_new || dirty || tick) {
                if (tick || dirty) {
                    uint32_t mv = board_battery_mv();
                    batt_pct = mv ? (int8_t)power_battery_pct(mv) : -1;  // 0 mV: no reading
                    glance_ms = now_ms();
                }
                render_glance(pal, batt_pct);
                show();
                drawn = true;
            }
        } else {
            nav.view.cfg = &cfg;
            nav.view.t_ms = now_ms();
            char url[sizeof panel_url] = "";  // "": the page says Wi-Fi isn't connected
            if (net_ip(ip)) snprintf(url, sizeof url, "http://%s/", ip);
            bool url_changed = strcmp(url, panel_url) != 0;  // Wi-Fi came or went while a page is open
            if (url_changed) {
                strcpy(panel_url, url);
                static ui_qr_t panel_qr;  // encoded once per address; size 0 (address only) without Wi-Fi or on failure
                if (url[0]) qr_encode(url, &panel_qr);
                else panel_qr.size = 0;
                ESP_LOGI(TAG, "panel QR: %d modules", panel_qr.size);
                nav.view.panel_qr = &panel_qr;
            }
            nav.view.panel_url = panel_url[0] ? panel_url : NULL;
            nav.view.panel_name = panel_name;
            if (dirty || cfg_changed || (url_changed && nav.view.page == UI_PAGE_PANEL)) {
                ui_render_menu(s_fb, &nav.view, pal);
                show();
                drawn = true;
            } else if (ui_menu_needs_tick(&nav.view)) {
                ui_render_menu(s_fb, &nav.view, pal);
                show();  // only the sliding text's rows changed
            }
        }
        if (drawn && redraw) board_backlight_set(level);  // light up only after a fresh frame
        if (backlight) shown_level = level;
        // A finger on the glance or settings: aim for a frame every FRAME_MS, counting the time spent drawing.
        uint32_t period = (t.count && nav.screen != UI_SCREEN_PET) ? FRAME_MS : LOOP_MS;
        uint32_t spent = now_ms() - loop_t0;
        vTaskDelay(pdMS_TO_TICKS(spent < period ? period - spent : 1));
    }
}

static void render_glance(const ui_palette_t *pal, int8_t batt_pct)
{
    static uint8_t ram[TC_PET_RAM_NIBBLES];
    tc_frame_t f;
    tc_get_frame(&f);
    tc_pet_t pet;
    tc_pet_decode(s_model, ram, tc_get_ram(ram, sizeof ram), &pet);
    int64_t unix_s = 0;
    bool known = net_now(&unix_s);
    time_t tt = (time_t)unix_s;
    struct tm tm = { 0 };
    localtime_r(&tt, &tm);
    char ip[16];
    ui_glance_t g = {
        .frame = &f, .pet = &pet, .model = s_model,
        .clock_known = known, .hour = (uint8_t)tm.tm_hour, .minute = (uint8_t)tm.tm_min,
        .batt_pct = batt_pct, .address = net_ip(ip) ? ip : "Wi-Fi off",
    };
    ui_render_glance(s_fb, &g, pal);
}

// After an overlay or at the start of play: the pet screen, and a BOOT press or touch still held is ignored.
static void resume_on_pet(ui_screen_t *screen, ui_gesture_t *gest, ui_nav_t *nav)
{
    ui_touch_t t;
    read_touch(&t);  // any finger still down, also on the LCD where it presses nothing: ignored until it lifts
    ui_screen_resume(screen, now_ms(), board_boot_down(), t.count != 0);
    ui_gesture_reset(gest);
    ui_nav_to_pet(nav);
}

esp_err_t ui_init(void)
{
    // PSRAM: internal RAM can't spare 115 KB once Wi-Fi runs (measured). The display driver
    // copies each frame into small internal DMA bands, so the framebuffer itself needn't be internal.
    s_fb = heap_caps_malloc(UI_W * UI_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) return ESP_ERR_NO_MEM;
    ui_sent_init(&s_sent);
    return ESP_OK;
}

void ui_message(const char *title, const char *body)
{
    if (!s_fb) return;
    st_settings_t cfg;
    settings_get(&cfg);
    ui_render_message(s_fb, title, body, ui_palette_by_id(cfg.palette));
    show();
    board_backlight_set(cfg.brightness);
}

static void read_touch(ui_touch_t *t)
{
    board_touch_t bt;
    board_touch_read(&bt);
    memset(t, 0, sizeof *t);
    t->count = bt.count;
    for (int i = 0; i < bt.count && i < 2; i++) { t->pt[i].x = bt.pt[i].x; t->pt[i].y = bt.pt[i].y; }
}

// The ROM picker's rows: one per distinct P1/P2 dump (first start, or the list opened during play; never both
// at once). s_row_entry[k] is row k's index into the scanned entries.
#define MAX_ROMS 16
static tc_rom_entry_t s_scan[MAX_ROMS];
static int s_row_entry[MAX_ROMS];
static ui_rom_row_t s_rows[MAX_ROMS];
static char s_row_sub[MAX_ROMS][80];  // "Gen N - <file>"; the list cuts long names with the ellipsis

static int rom_rows(const tc_rom_entry_t *roms, int n)
{
    int m = tc_rom_distinct_valid(roms, n, s_row_entry, MAX_ROMS);
    for (int k = 0; k < m; k++) {
        const tc_rom_entry_t *r = &roms[s_row_entry[k]];
        const char *longname = tc_model_long_name(r->model);
        if (longname && strcmp(longname, tc_model_name(r->model)) != 0)
            snprintf(s_row_sub[k], sizeof s_row_sub[k], "%s - %s", longname, r->name);
        else
            snprintf(s_row_sub[k], sizeof s_row_sub[k], "%s", r->name);
        s_rows[k] = (ui_rom_row_t){ tc_model_name(r->model), s_row_sub[k] };
    }
    return m;
}

// During play: scan the SD card and mark the running ROM's row (by the cached copy's SHA-1).
static void load_rom_rows(ui_menu_view_t *v)
{
    int n = rom_store_scan(s_scan, MAX_ROMS), m = rom_rows(s_scan, n);
    v->rom_current = -1;
    uint8_t active[20];
    uint8_t *rom = malloc(TC_ROM_MAX_BYTES);  // only to learn the active SHA-1
    size_t rom_len;
    if (rom && rom_store_cached(rom, &rom_len, active))
        for (int k = 0; k < m; k++)
            if (!memcmp(s_scan[s_row_entry[k]].sha1, active, 20)) v->rom_current = k;
    free(rom);
    v->roms = s_rows;
    v->rom_count = m;
    ESP_LOGI(TAG, "ROM list: %d pets, current row %d", m, v->rom_current);
}

int ui_pick_rom(const tc_rom_entry_t *roms, int n)
{
    int m = rom_rows(roms, n);
    if (!m || !s_fb) return -1;
    ui_nav_t nav;
    ui_nav_init(&nav);
    ui_nav_open_roms(&nav, false);
    nav.view.rom_first = true;
    nav.view.roms = s_rows;
    nav.view.rom_count = m;
    ui_gesture_t gest = { 0 };
    ui_gesture_reset(&gest);  // a finger already down when this shows is ignored until it lifts
    st_settings_t cfg;
    bool dirty = true;
    for (;;) {
        settings_get(&cfg);
        if (dirty) {
            nav.view.cfg = &cfg;
            nav.view.t_ms = now_ms();
            ui_render_menu(s_fb, &nav.view, ui_palette_by_id(cfg.palette));
            show();
            board_backlight_set(cfg.brightness);
            dirty = false;
        }
        ui_touch_t t;
        read_touch(&t);
        ui_gevent_t ev;
        ui_gesture_feed(&gest, UI_GMODE_MENU, &t, now_ms(), &ev);
        st_settings_t edited;
        bool display_changed;
        nav.view.t_ms = now_ms();
        ui_act_t a = ui_nav_step(&nav, &ev, false, &cfg, &edited, &display_changed);
        if (a == UI_ACT_ROM_SWITCH && nav.view.rom_pick >= 0 && nav.view.rom_pick < m) {
            ESP_LOGI(TAG, "first start: picked %s", roms[s_row_entry[nav.view.rom_pick]].name);
            return s_row_entry[nav.view.rom_pick];
        }
        dirty = a == UI_ACT_REDRAW;  // a drag scrolled the list
        vTaskDelay(pdMS_TO_TICKS(t.count ? FRAME_MS : LOOP_MS));
    }
}

// Switch confirmed on the ROM list (row of s_rows): save the pet, store the new ROM and restart. The switch
// itself is shared with the remote API (rom_switch_to); the messages stay here.
static void switch_rom(int row, int rows)
{
    if (row < 0 || row >= rows || row >= MAX_ROMS) return;  // rows: this scan's count (older rows may linger)
    const char *name = s_scan[s_row_entry[row]].name;
    ESP_LOGI(TAG, "switching to %s", name);
    ui_message("Switching", "Saving your pet...");
    esp_err_t err = rom_switch_to(name);
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
