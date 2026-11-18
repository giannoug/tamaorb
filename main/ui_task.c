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
#include "saver.h"
#include "factory_reset.h"
#include "tc_time.h"

#define LOOP_MS        30
#define FRAME_MS       16   // while a finger is on the glance or settings: ~60 frames a second

static const char *TAG = "ui";
static tc_model_t s_model;
static uint16_t *s_fb;
static char s_running_name[PS_NAME_MAX + 1], s_new_name[PS_NAME_MAX + 1];
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
static void load_pet_rows(ui_menu_view_t *v);
static void prepare_new_name(const ui_menu_view_t *v);
static bool pet_action(ui_act_t act, ui_nav_t *nav);
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

// The running pet's name, once the saver knows which pet runs (it doesn't yet while the UI task starts).
static void running_name(void)
{
    pet_ref_t run;
    if (s_running_name[0] || !saver_running(run.sha1, &run.id)) return;
    if (pet_store_name(&run, s_running_name) != ESP_OK) s_running_name[0] = 0;
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

    // Before the emulator runs: a start-up screen, the resume question (EMU_WAIT), then the catch-up screen with Skip.
    // The question and the catch-up dim and time out like the pet screen; a tap that wakes the screen does nothing else.
    uint32_t last_progress_draw = 0, guard_ms = 0;
    bool boot_drawn = false, lit = true, dirty = true, dimmed_pre = false;
    ui_skip_t skip = UI_SKIP_BUTTON;
    emu_phase_t shown_phase = EMU_BOOT;
    ui_resume_view_t rq = { 0 };
    static char back_name[PS_NAME_MAX + 1];
    ui_gesture_t pre = { 0 };
    ui_gesture_reset(&pre);
    st_settings_t cfg;
    settings_get(&cfg);
    screen_lit(true);  // lit: no light sleep (it would stop the backlight PWM)
    while (emu_phase() != EMU_RUN) {
        settings_get(&cfg);
        const ui_palette_t *pal = ui_palette_by_id(cfg.palette);
        if (s_low_battery) {
            ui_render_low_battery(s_fb, pal);
            show();
            board_backlight_set(cfg.brightness);
            screen_lit(true);
            vTaskSuspend(NULL);  // the power task puts the device to sleep
        }
        emu_phase_t ph = emu_phase();
        if (ph == EMU_BOOT) {
            if (!boot_drawn) {
                ui_render_message(s_fb, "TamaOrb", "Starting...", pal);
                show();
                board_backlight_set(cfg.brightness);
                boot_drawn = true;
            }
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }
        if (ph != shown_phase) {  // the question or the catch-up just began: fresh, lit, guarded against a stray tap
            shown_phase = ph;
            skip = UI_SKIP_BUTTON;
            dirty = true;
            guard_ms = now_ms();
            ui_screen_wake(&screen, now_ms());
            running_name();
            if (ph == EMU_WAIT) {
                pet_ref_t prev;
                rq = (ui_resume_view_t){ s_running_name, emu_wait_gap_s(), 0, pet_prev(&prev, back_name) ? back_name : NULL };
                rq.ff_s = tc_ff_estimate_s(rq.gap_s, emu_catchup_speed());
            }
        }
        ui_touch_t t;
        read_touch(&t);
        ui_gevent_t ev;
        ui_gesture_feed(&pre, UI_GMODE_MENU, &t, now_ms(), &ev);
        bool usb = board_usb_present();
        ui_screen_out_t out = ui_screen_update(&screen, now_ms(), t.count ? UI_TOUCH_PRESENT : 0, board_boot_down(), false,
                                               false, usb ? 0 : cfg.off_battery_s * 1000u);
        if (screen.swallowing) {
            ui_gesture_reset(&pre);
            ev.kind = UI_G_NONE;
        }
        if (out.backlight != lit) {
            lit = out.backlight;
            screen_lit(lit);
            if (!lit) board_backlight_set(0);
            else dirty = true;
        }
        if (lit && ev.kind == UI_G_TAP && now_ms() - guard_ms >= UI_CONFIRM_GUARD_MS) {
            if (ph == EMU_WAIT) {
                ui_resume_hit_t h = ui_resume_at(&rq, ev.x, ev.y);
                if (h == UI_RESUME_RESUME) emu_answer(EMU_ANSWER_RESUME);
                else if (h == UI_RESUME_FF) emu_answer(EMU_ANSWER_FF);
                else if (h == UI_RESUME_BACK) {
                    ui_message("Going back", back_name);
                    esp_err_t e = pet_switch_back();
                    if (e == ESP_OK) for (;;) vTaskDelay(pdMS_TO_TICKS(1000));  // until the restart
                    ui_message("Not going back", e == ESP_ERR_NOT_FOUND ? "That pet's ROM is not\non the SD card." : "Try again in a\nmoment.");
                    vTaskDelay(pdMS_TO_TICKS(2500));
                    dirty = true;
                }
            } else if (ph == EMU_CATCHUP) {
                ui_cu_hit_t h = ui_catchup_at(skip, ev.x, ev.y);
                if (h == UI_CU_SKIP) { skip = UI_SKIP_CONFIRM; guard_ms = now_ms(); dirty = true; }
                else if (h == UI_CU_GO_ON) { skip = UI_SKIP_BUTTON; dirty = true; }
                else if (h == UI_CU_STOP) { emu_skip(); skip = UI_SKIP_BUTTON; dirty = true; }
            }
        }
        uint32_t dim_s = usb ? cfg.dim_usb_s : cfg.dim_battery_s;
        bool dim = ui_screen_dimmed(&screen, now_ms(), dim_s ? dim_s * 1000u : UINT32_MAX);
        if (lit && !dirty && dim != dimmed_pre) board_backlight_set(dim ? st_effective_dim(&cfg) : cfg.brightness);
        dimmed_pre = dim;
        if (lit && ph == EMU_WAIT && dirty) {
            ui_render_resume(s_fb, &rq, pal);
            show();
            board_backlight_set(dim ? st_effective_dim(&cfg) : cfg.brightness);
            dirty = false;
        } else if (lit && ph == EMU_CATCHUP && (dirty || now_ms() - last_progress_draw >= 1000)) {
            uint64_t done, total;
            uint32_t eta;
            emu_catchup_progress(&done, &total, &eta);
            ui_render_catchup(s_fb, s_running_name, done, total, eta, skip, pal);
            show();
            board_backlight_set(dim ? st_effective_dim(&cfg) : cfg.brightness);
            last_progress_draw = now_ms();
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(t.count ? FRAME_MS : LOOP_MS));
    }
    ui_gesture_t gest = { 0 };
    ui_nav_t nav;
    ui_nav_init(&nav);
    resume_on_pet(&screen, &gest, &nav);
    screen_lit(false);
    backlight = false;  // force a fresh pet frame and backlight on
    running_name();
    ESP_LOGI(TAG, "showing the pet (%s)", s_running_name);

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
        if (ui_screen_long_press(&screen)) {  // from any screen, lit or not: the Pets list
            tc_post_buttons(0);  // ui_screen_update already woke the screen when the long press fired
            ui_nav_open_pets(&nav, true);
            ui_gesture_reset(&gest);  // a touch held now belongs to no row
            act = UI_ACT_REDRAW;
        }
        nav.view.running_name = s_running_name;
        nav.view.new_name = s_new_name;
        if (nav.screen == UI_SCREEN_MENU && nav.view.page == UI_PAGE_ROM && !nav.view.roms) {
            load_rom_rows(&nav.view);  // the SD scan, once per opening
            act = UI_ACT_REDRAW;
        }
        if (nav.screen == UI_SCREEN_MENU && !nav.view.pets &&
            (nav.view.page == UI_PAGE_PETS || nav.view.page == UI_PAGE_ROM)) {
            load_pet_rows(&nav.view);  // NVS and the SD card, once per opening (the New pet list needs the names)
            act = UI_ACT_REDRAW;
        }
        if (nav.screen == UI_SCREEN_MENU && nav.view.page == UI_PAGE_ROM_CONFIRM) prepare_new_name(&nav.view);
        if (out.backlight && nav.screen == UI_SCREEN_MENU && nav.view.page == UI_PAGE_RESET) {
            ui_act_t h = ui_nav_hold(&nav, t.count > 0, t.pt[0].x, t.pt[0].y, now_ms());
            if (h == UI_ACT_FACTORY_RESET) {
                ESP_LOGW(TAG, "factory reset held");
                if (factory_reset_request() == ESP_OK) {
                    ui_message("Erasing...", "The ball restarts\nlike new.");
                    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));  // until the restart
                }
                ui_message("Not erased", "The ball is already\nrestarting. Try again\nafter it.");
                vTaskDelay(pdMS_TO_TICKS(3000));
                ui_nav_to_pet(&nav);
            }
            if (h != UI_ACT_NONE && act == UI_ACT_NONE) act = UI_ACT_REDRAW;
        }
        if (act == UI_ACT_CLOCK_SYNC) {
            nav.view.clock_busy = false;
            pet_clock_status_t st = pet_clock_start(&s_clock_id);  // re-checked now: time or the phase may have gone
            s_clock_pending = st == PET_CLOCK_OK;
            if (!s_clock_pending) ESP_LOGW(TAG, "pet clock not set (%d)", (int)st);
            nav.view.clock_why = clock_why();
            act = UI_ACT_REDRAW;
        }
        if (act == UI_ACT_PET_SWITCH || act == UI_ACT_PET_NEW || act == UI_ACT_PET_DELETE) {
            tc_post_buttons(0);
            if (pet_action(act, &nav)) {  // restarts the ball, or returns after saying why it didn't
                resume_on_pet(&screen, &gest, &nav);
                screen_lit(false);
                backlight = false;  // force a fresh pet frame
                continue;
            }
            ui_gesture_reset(&gest);
            act = UI_ACT_REDRAW;
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

// ---- ROM rows: the first-start picker (one per distinct playable dump) and the New pet list (one per model) ----
#define MAX_ROMS 16
static tc_rom_entry_t s_scan[MAX_ROMS];
static int s_row_entry[MAX_ROMS];       // first start: row k's index into the scanned entries
static ui_rom_row_t s_rows[MAX_ROMS];
static char s_row_sub[MAX_ROMS][80];    // "Gen N - <file>"; the list cuts long names with the ellipsis
static uint8_t s_row_sha[MAX_ROMS][20]; // New pet: row k's ROM (unset for a model not on the card)

static void rom_sub(const tc_rom_entry_t *r, char *out, size_t cap)
{
    const char *longname = tc_model_long_name(r->model);
    if (longname && strcmp(longname, tc_model_name(r->model)) != 0) snprintf(out, cap, "%s - %s", longname, r->name);
    else snprintf(out, cap, "%s", r->name);
}

static int rom_rows(const tc_rom_entry_t *roms, int n)
{
    int m = tc_rom_distinct_valid(roms, n, s_row_entry, MAX_ROMS);
    for (int k = 0; k < m; k++) {
        const tc_rom_entry_t *r = &roms[s_row_entry[k]];
        rom_sub(r, s_row_sub[k], sizeof s_row_sub[k]);
        s_rows[k] = (ui_rom_row_t){ tc_model_name(r->model), s_row_sub[k], r->name, true };
    }
    return m;
}

// The New pet list: P1, P2, Angel, Mothra, each from its dump on the card, greyed when there is none.
static void load_rom_rows(ui_menu_view_t *v)
{
    static const tc_model_t MODELS[] = { TC_MODEL_P1, TC_MODEL_P2, TC_MODEL_ANGEL, TC_MODEL_MOTHRA };
    int n = rom_store_scan(s_scan, MAX_ROMS), k = 0;
    for (size_t mi = 0; mi < sizeof MODELS / sizeof MODELS[0]; mi++, k++) {
        int found = -1;
        for (int i = 0; i < n && found < 0; i++)
            if (s_scan[i].model == MODELS[mi]) found = i;
        if (found >= 0) {
            rom_sub(&s_scan[found], s_row_sub[k], sizeof s_row_sub[k]);
            memcpy(s_row_sha[k], s_scan[found].sha1, 20);
            s_rows[k] = (ui_rom_row_t){ tc_model_name(MODELS[mi]), s_row_sub[k], s_scan[found].name, true };
        } else {
            s_rows[k] = (ui_rom_row_t){ tc_model_name(MODELS[mi]), "not on the SD card", NULL, false };
        }
    }
    v->roms = s_rows;
    v->rom_count = k;
}

// ---- Pet rows ----
static pet_info_t s_pets[PET_STORE_MAX];
static ui_pet_row_t s_pet_rows[PET_STORE_MAX];
static char s_pet_sub[PET_STORE_MAX][40], s_pet_age[PET_STORE_MAX][32], s_pet_away[PET_STORE_MAX][24];
static int s_pet_n;

static bool is_running_pet(const pet_ref_t *p)
{
    uint8_t sha[20], id;
    return saver_running(sha, &id) && id == p->id && !memcmp(sha, p->sha1, 20);
}

static void load_pet_rows(ui_menu_view_t *v)
{
    s_pet_n = pet_store_list(s_pets, PET_STORE_MAX);
    int64_t now = 0;
    bool trusted = net_now(&now);
    v->pet_current = -1;
    for (int i = 0; i < s_pet_n; i++) {
        const pet_info_t *p = &s_pets[i];
        bool running = is_running_pet(&p->ref);
        const char *longname = tc_model_long_name(p->model);
        char age[12] = "", short_away[12] = "";
        if (p->egg) snprintf(age, sizeof age, "egg");
        else if (p->age_years >= 0) snprintf(age, sizeof age, "%d yr", p->age_years);
        snprintf(s_pet_age[i], sizeof s_pet_age[i], "%s%s%s", longname ? longname : "", age[0] ? " - " : "", age);
        if (running) {
            snprintf(s_pet_away[i], sizeof s_pet_away[i], "running now");
            snprintf(short_away, sizeof short_away, "now");
            v->pet_current = i;
        } else if (trusted && p->has_save && p->save_trusted && now > p->save_unix) {
            char d[16];
            ui_fmt_away((uint64_t)(now - p->save_unix), false, d, sizeof d);
            snprintf(s_pet_away[i], sizeof s_pet_away[i], "away %s", d);
            ui_fmt_away((uint64_t)(now - p->save_unix), true, short_away, sizeof short_away);
        } else {
            snprintf(s_pet_away[i], sizeof s_pet_away[i], "%s", p->has_save ? "away" : "not started yet");
        }
        snprintf(s_pet_sub[i], sizeof s_pet_sub[i], "%s%s%s", s_pet_age[i], short_away[0] ? " - " : "", short_away);
        s_pet_rows[i] = (ui_pet_row_t){ p->name, s_pet_sub[i], s_pet_age[i], s_pet_away[i], p->rom_on_card };
    }
    v->pets = s_pet_rows;
    v->pet_count = s_pet_n;
    ESP_LOGI(TAG, "pets: %d, running row %d", s_pet_n, v->pet_current);
}

// The name a new pet of rows[pick] would get, for the confirm (pet_store_create picks the same).
static void prepare_new_name(const ui_menu_view_t *v)
{
    const char *names[PET_STORE_MAX];
    for (int i = 0; i < s_pet_n; i++) names[i] = s_pets[i].name;
    const char *model = v->roms && v->rom_pick >= 0 && v->rom_pick < v->rom_count ? v->roms[v->rom_pick].name : "Pet";
    ps_name_auto(model, names, s_pet_n, s_new_name);
}

int ui_pick_rom(const tc_rom_entry_t *roms, int n)
{
    int m = rom_rows(roms, n);
    if (!m || !s_fb) return -1;
    ui_nav_t nav;
    ui_nav_init(&nav);
    ui_nav_open_roms(&nav);
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

// A switch or a new pet: the ball saves and restarts (this keeps the message up until then), or says why not.
static void after_switch(esp_err_t err)
{
    if (err == ESP_OK) {
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (err == ESP_FAIL) ui_message("Not switching", "Couldn't save your\npet, so it stays.");
    else if (err == ESP_ERR_NOT_FOUND) ui_message("Not switching", "That ROM is not on\nthe SD card.");
    else if (err == ESP_ERR_NO_MEM) ui_message("No room", "Ten pets of this ROM\nalready, or storage\nis full.");
    else ui_message("Switch failed", "Try again in a\nmoment.");
    vTaskDelay(pdMS_TO_TICKS(2500));
}

// What a confirmed pet action does. Returns true when the pet screen should show next (a message was shown).
static bool pet_action(ui_act_t act, ui_nav_t *nav)
{
    ui_menu_view_t *v = &nav->view;
    if (act == UI_ACT_PET_SWITCH && v->pet_pick >= 0 && v->pet_pick < s_pet_n) {
        ESP_LOGI(TAG, "switching to %s", s_pets[v->pet_pick].name);
        ui_message("Switching", "Saving your pet...");
        after_switch(pet_switch_to(&s_pets[v->pet_pick].ref));
        return true;
    }
    if (act == UI_ACT_PET_NEW && v->roms && v->rom_pick >= 0 && v->rom_pick < v->rom_count && v->roms[v->rom_pick].enabled) {
        ESP_LOGI(TAG, "new pet of %s", v->roms[v->rom_pick].file);
        ui_message("New pet", "Saving your pet...");
        pet_ref_t created;
        after_switch(pet_new(s_row_sha[v->rom_pick], &created));
        return true;
    }
    if (act == UI_ACT_PET_DELETE && v->pet_pick >= 0 && v->pet_pick < s_pet_n) {
        esp_err_t err = pet_delete(&s_pets[v->pet_pick].ref);
        ESP_LOGI(TAG, "delete %s: %s", s_pets[v->pet_pick].name, esp_err_to_name(err));
        if (err != ESP_OK) {
            ui_message("Not deleted", err == ESP_ERR_INVALID_STATE ? "That pet is running." : "Try again in a\nmoment.");
            vTaskDelay(pdMS_TO_TICKS(2000));
            return true;
        }
        ui_nav_open_pets(nav, nav->pets_from_pet);  // the list again, reloaded
        return false;
    }
    return false;
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
