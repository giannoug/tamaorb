// SPDX-License-Identifier: GPL-3.0-or-later
#include "unity.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>

static ui_nav_t nav;
static st_settings_t cfg, out;
static bool dc;

void setUp(void)
{
    st_defaults(&cfg, 40);
    ui_nav_init(&nav);
    memset(&out, 0, sizeof out);
    dc = false;
}
void tearDown(void) {}

static ui_act_t step(ui_gkind_t k, int x, int y, int dx, int dy, bool boot)
{
    ui_gevent_t ev = { k, (int16_t)x, (int16_t)y, (int16_t)dx, (int16_t)dy, false };
    nav.view.t_ms += 1000;  // deliberate inputs, well apart (see test_confirm_ignores_quick_second_tap)
    return ui_nav_step(&nav, &ev, boot, &cfg, &out, &dc);
}
static ui_act_t tap(int x, int y) { return step(UI_G_TAP, x, y, 0, 0, false); }
// A drag event of a touch that went down at (sx, sy); first: the touch's first event.
static ui_act_t drag_from(int sx, int sy, int x, int y, int dy, bool first)
{
    ui_gevent_t ev = { UI_G_DRAG, (int16_t)x, (int16_t)y, 0, (int16_t)dy, false, (int16_t)sx, (int16_t)sy, first };
    return ui_nav_step(&nav, &ev, false, &cfg, &out, &dc);
}
static ui_act_t drag(int x, int y, int dy) { return drag_from(x, y, x, y, dy, true); }
static void to_menu(void) { step(UI_G_SWIPE_UP, 120, 230, 0, -80, false); }
// Scrolls the current list until row i is on screen; returns its y, or -1.
static int row_y(int i)
{
    for (int sc = 0; sc <= ui_menu_scroll_max(&nav.view); sc += 4) {
        nav.view.scroll = sc;
        for (int y = 0; y < UI_H; y++)
            if (ui_menu_row_at(&nav.view, 120, y) == i) return y;
    }
    return -1;
}
static void tap_row(int i)
{
    int y = row_y(i);
    TEST_ASSERT_TRUE_MESSAGE(y >= 0, "row not found");
    tap(120, y);
}
static void open_page(int row) { to_menu(); tap_row(row); }
static bool find_ctl(int ctl, int *x, int *y, int want)
{
    for (int yy = 0; yy < UI_H; yy++)
        for (int xx = 0; xx < UI_W; xx++) {
            int v;
            if (ui_menu_control_at(&nav.view, xx, yy, &v) == ctl && v == want) { *x = xx; *y = yy; return true; }
        }
    return false;
}

// ---- Real touch strokes: ui_gesture_feed + ui_nav_step, as ui_task runs them ----
static ui_gesture_t gest;
static int settings_events;  // UI_ACT_SETTINGS results since the last reset; each one is applied to cfg

static void feed_touch(int count, int x, int y)
{
    ui_touch_t t = { (uint8_t)count, { { (int16_t)x, (int16_t)y }, { 0, 0 } } };
    ui_gmode_t mode = nav.screen == UI_SCREEN_PET ? UI_GMODE_PET : UI_GMODE_MENU;
    ui_gevent_t ev;
    ui_gesture_feed(&gest, mode, &t, 0, &ev);
    if (ui_nav_step(&nav, &ev, false, &cfg, &out, &dc) == UI_ACT_SETTINGS) {
        cfg = out;  // the device stores it and the page shows it from the next loop
        settings_events++;
    }
}

// A finger from (x0,y0) to (x1,y1) in samples about `step` px apart; lifts at the end if `lift`.
static void stroke(int x0, int y0, int x1, int y1, int step, bool lift)
{
    int dx = x1 - x0, dy = y1 - y0, len = abs(dx) > abs(dy) ? abs(dx) : abs(dy), n = len / step;
    if (n < 1) n = 1;
    for (int k = 0; k <= n; k++) feed_touch(1, x0 + dx * k / n, y0 + dy * k / n);
    if (lift) feed_touch(0, 0, 0);
}

static void test_swipe_down_on_dimming_changes_nothing(void)
{
    static const int xs[] = { 50, 90, 130, 170, 200 };
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        setUp();
        memset(&gest, 0, sizeof gest);
        open_page(UI_ROW_DIMMING);
        st_settings_t before = cfg;
        settings_events = 0;
        stroke(xs[i], 20, xs[i], 80, 6, true);  // from the top band; 6 px per sample, the swipe completes at y=80
        TEST_ASSERT_EQUAL(0, settings_events);
        TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
        TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    }
}

static void test_slow_swipe_up_on_sound_changes_nothing(void)
{
    memset(&gest, 0, sizeof gest);
    open_page(UI_ROW_SOUND);
    st_settings_t before = cfg;
    settings_events = 0;
    stroke(200, 228, 200, 168, 6, true);  // from the bottom band across the volume track; swipes at y=168
    TEST_ASSERT_EQUAL(0, settings_events);
    TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

static void test_drag_from_off_control_crossing_volume_changes_nothing(void)
{
    memset(&gest, 0, sizeof gest);
    open_page(UI_ROW_SOUND);
    st_settings_t before = cfg;
    int v;
    TEST_ASSERT_EQUAL(-1, ui_menu_control_at(&nav.view, 150, 110, &v));
    settings_events = 0;
    stroke(150, 110, 150, 200, 6, true);
    TEST_ASSERT_EQUAL(0, settings_events);
    TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

static void test_drag_started_on_slider_adjusts_it(void)
{
    memset(&gest, 0, sizeof gest);
    open_page(UI_ROW_SOUND);
    int x0, y, x1;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_VOLUME, &x0, &y, 20));
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_VOLUME, &x1, &y, 60));
    settings_events = 0;
    stroke(x0, y, x1, y + 3, 4, true);
    TEST_ASSERT_TRUE(settings_events > 0);
    TEST_ASSERT_EQUAL(60, cfg.volume);
}

static void test_lost_release_does_not_lock_next_drag(void)
{
    memset(&gest, 0, sizeof gest);
    open_page(UI_ROW_DIMMING);
    int x, y, xu, yu, xu2;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_DIM_LEVEL, &x, &y, 30));
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_DIM_USB, &xu, &yu, 30));
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_DIM_USB, &xu2, &yu, 600));
    stroke(x, y, x + 40, y, 4, false);   // dragging the dim level ...
    ui_gesture_reset(&gest);             // ... when the screen swallows the touch: no release reaches the nav
    feed_touch(1, x + 40, y);
    feed_touch(0, 0, 0);
    settings_events = 0;
    stroke(xu, yu, xu2, yu, 4, true);    // the next drag starts on "Dim on USB"
    TEST_ASSERT_TRUE(settings_events > 0);
    TEST_ASSERT_EQUAL(600, cfg.dim_usb_s);
}

static void test_transitions(void)
{
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false));
    TEST_ASSERT_EQUAL(UI_SCREEN_GLANCE, nav.screen);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(120, 120));
    TEST_ASSERT_EQUAL(UI_SCREEN_GLANCE, nav.screen);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, drag(120, 120, 30));
    step(UI_G_SWIPE_UP, 120, 230, 0, -80, false);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false);
    step(UI_G_NONE, 0, 0, 0, 0, true);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    to_menu();
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
    step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    to_menu();
    step(UI_G_NONE, 0, 0, 0, 0, true);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
}

static void test_pages_and_back(void)
{
    open_page(UI_ROW_SOUND);
    TEST_ASSERT_EQUAL(UI_PAGE_SOUND, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
    tap_row(UI_ROW_TZ);
    TEST_ASSERT_EQUAL(UI_PAGE_TZ_REGION, nav.view.page);
    tap(120, 5);
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
    tap_row(UI_ROW_TZ);
    tap_row(0);
    TEST_ASSERT_EQUAL(UI_PAGE_TZ_CITY, nav.view.page);
    TEST_ASSERT_NOT_NULL(nav.view.region);
    tap(120, 5);
    TEST_ASSERT_EQUAL(UI_PAGE_TZ_REGION, nav.view.page);
}

// ---- ROM picker pages ----
static const ui_rom_row_t ROMS[2] = { { "P1", "Gen 1 - tama.bin" }, { "P2", "Gen 2 - tamag2.bin" } };

// What ui_task does when the ROM page opens: fill the rows.
static void load_roms(int count, int current)
{
    nav.view.roms = ROMS;
    nav.view.rom_count = count;
    nav.view.rom_current = current;
}
static void open_roms(bool from_pet, int current)
{
    if (from_pet) ui_nav_open_roms(&nav, true);
    else open_page(UI_ROW_ROM);
    load_roms(2, current);
}
static bool find_any_ctl(int ctl, int *x, int *y)
{
    for (int yy = 0; yy < UI_H; yy++)
        for (int xx = 0; xx < UI_W; xx++)
            if (ui_menu_control_at(&nav.view, xx, yy, NULL) == ctl) { *x = xx + 4; *y = yy + 4; return true; }
    return false;
}

static void test_settings_rom_row_opens_page(void)
{
    to_menu();
    int y = row_y(UI_ROW_ROM);
    TEST_ASSERT_TRUE(y >= 0);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, y));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    TEST_ASSERT_NULL(nav.view.roms);
    TEST_ASSERT_FALSE(nav.rom_from_pet);
    TEST_ASSERT_FALSE(nav.view.rom_first);
}

static void test_tap_current_closes(void)
{
    open_roms(false, 0);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, row_y(0)));
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    TEST_ASSERT_NULL(nav.view.roms);
}

static void test_tap_other_confirms(void)
{
    open_roms(false, 0);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, row_y(1)));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM_CONFIRM, nav.view.page);
    TEST_ASSERT_EQUAL(1, nav.view.rom_pick);
    TEST_ASSERT_TRUE(nav.view.roms == ROMS);
    TEST_ASSERT_EQUAL(2, nav.view.rom_count);
}

static void test_confirm_cancel_and_switch(void)
{
    open_roms(false, 0);
    tap(120, row_y(1));
    int x, y;
    TEST_ASSERT_TRUE(find_any_ctl(UI_CTL_ROM_CANCEL, &x, &y));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(x, y));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    tap(120, row_y(1));
    TEST_ASSERT_TRUE(find_any_ctl(UI_CTL_ROM_SWITCH, &x, &y));
    TEST_ASSERT_EQUAL(UI_ACT_ROM_SWITCH, tap(x, y));
    TEST_ASSERT_EQUAL(1, nav.view.rom_pick);
}

static void test_confirm_boot_and_swipe_return_to_list(void)
{
    open_roms(true, 0);
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_NONE, 0, 0, 0, 0, true));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    // a drag across the Switch pill neither switches nor edits a setting
    tap(120, row_y(1));
    int x, y;
    TEST_ASSERT_TRUE(find_any_ctl(UI_CTL_ROM_SWITCH, &x, &y));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, drag_from(x, y, x + 2, y, 0, true));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, drag_from(x, y, x + 6, y, 0, false));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM_CONFIRM, nav.view.page);
}

// Review finding: the Switch pill overlaps where a lower list row sat, so a double tap on that row must not
// switch pets: the confirm page ignores its pills for UI_CONFIRM_GUARD_MS after it opens.
static ui_act_t tap_at_ms(int x, int y, uint32_t t_ms)
{
    ui_gevent_t ev = { UI_G_TAP, (int16_t)x, (int16_t)y, 0, 0, false, (int16_t)x, (int16_t)y, true };
    nav.view.t_ms = t_ms;
    return ui_nav_step(&nav, &ev, false, &cfg, &out, &dc);
}

static void test_confirm_ignores_quick_second_tap(void)
{
    static const ui_rom_row_t THREE[3] = { { "P1", "Gen 1 - a.bin" }, { "P2", "Gen 2 - b.bin" }, { "P2", "Gen 2 - c.bin" } };
    ui_nav_open_roms(&nav, true);
    nav.view.roms = THREE;
    nav.view.rom_count = 3;
    nav.view.rom_current = 0;
    // a point that is on row 2 of the list and on the Switch pill of the confirm page
    int px = -1, py = -1;
    for (int y = 0; y < UI_H && px < 0; y++)
        for (int x = 0; x < UI_W && px < 0; x++) {
            nav.view.page = UI_PAGE_ROM_CONFIRM;
            bool on_pill = ui_menu_control_at(&nav.view, x, y, NULL) == UI_CTL_ROM_SWITCH;
            nav.view.page = UI_PAGE_ROM;
            if (on_pill && ui_menu_row_at(&nav.view, x, y) == 2) { px = x; py = y; }
        }
    TEST_ASSERT_TRUE_MESSAGE(px >= 0, "row 2 and the Switch pill no longer overlap");
    const uint32_t t0 = 50000;
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_at_ms(px, py, t0));          // first tap: the confirm page
    TEST_ASSERT_EQUAL(UI_PAGE_ROM_CONFIRM, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap_at_ms(px, py, t0 + 250));      // the double tap's second half
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap_at_ms(px, py, t0 + UI_CONFIRM_GUARD_MS - 1));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM_CONFIRM, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_ROM_SWITCH, tap_at_ms(px, py, t0 + UI_CONFIRM_GUARD_MS));  // a deliberate tap
}

static void test_rom_back(void)
{
    open_roms(false, 0);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
    ui_nav_init(&nav);
    open_roms(true, 0);
    TEST_ASSERT_TRUE(nav.rom_from_pet);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    open_roms(false, 0);
    step(UI_G_NONE, 0, 0, 0, 0, true);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    open_roms(false, 0);
    step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
}

static void test_rom_first_start(void)
{
    ui_nav_open_roms(&nav, false);
    nav.view.rom_first = true;
    load_roms(2, -1);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, step(UI_G_NONE, 0, 0, 0, 0, true));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_ROM_SWITCH, tap(120, row_y(1)));
    TEST_ASSERT_EQUAL(1, nav.view.rom_pick);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);  // no confirm page
}

static void test_rom_current_unlisted(void)
{
    open_roms(false, -1);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, row_y(0)));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM_CONFIRM, nav.view.page);
    TEST_ASSERT_EQUAL(0, nav.view.rom_pick);
}

static void test_rom_page_empty(void)
{
    open_page(UI_ROW_ROM);
    for (int y = 0; y < UI_H; y += 4)  // not loaded yet: nothing to tap below the header
        if (y >= 30) TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(120, y));
    load_roms(0, -1);
    for (int y = 30; y < UI_H; y += 4) TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(120, y));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
}

static void test_edit_changes_only_touched_key(void)
{
    open_page(UI_ROW_SOUND);          // the view sees brightness 80 ...
    cfg.brightness = 80;
    ui_gevent_t none = { UI_G_NONE, 0, 0, 0, 0, false };
    ui_nav_step(&nav, &none, false, &cfg, &out, &dc);
    cfg.brightness = 33;              // ... but the latest settings have 33
    int x, y;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_VOLUME, &x, &y, 70));
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, drag(x, y, 0));
    st_settings_t want = cfg;
    want.volume = 70;
    TEST_ASSERT_EQUAL_MEMORY(&want, &out, sizeof out);
    TEST_ASSERT_EQUAL(33, out.brightness);
    TEST_ASSERT_FALSE(dc);
}

static void test_mute_toggle_keeps_volume(void)
{
    open_page(UI_ROW_SOUND);
    int x, y;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_MUTE, &x, &y, 1));
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, tap(x, y));
    TEST_ASSERT_TRUE(out.muted);
    TEST_ASSERT_EQUAL(cfg.volume, out.volume);
    cfg = out;
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, tap(x, y));
    TEST_ASSERT_FALSE(out.muted);
    TEST_ASSERT_EQUAL(40, out.volume);
}

static void test_dim_slider_snaps(void)
{
    open_page(UI_ROW_DIMMING);
    int x, y;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_DIM_USB, &x, &y, 300));
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, tap(x, y));
    TEST_ASSERT_EQUAL(300, out.dim_usb_s);
    TEST_ASSERT_FALSE(dc);
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_OFF_BATTERY, &x, &y, 1800));
    tap(x, y);
    TEST_ASSERT_EQUAL(1800, out.off_battery_s);
}

static void test_drag_stays_on_its_slider(void)
{
    open_page(UI_ROW_DIMMING);
    int x, y, x2, y2;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_DIM_LEVEL, &x, &y, 50));
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, drag(x, y, 0));
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_DIM_USB, &x2, &y2, 60));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, drag_from(x, y, x2, y2, 0, false));
}

static void test_brightness_and_palette(void)
{
    open_page(UI_ROW_BRIGHTNESS);
    int x, y;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_BRIGHTNESS, &x, &y, 10));
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, drag(x, y, 0));
    TEST_ASSERT_EQUAL(10, out.brightness);
    TEST_ASSERT_TRUE(dc);
    tap(120, 5);
    tap_row(UI_ROW_PALETTE);
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_PALETTE, &x, &y, ST_PALETTE_NIGHT));
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, tap(x, y));
    TEST_ASSERT_EQUAL(ST_PALETTE_NIGHT, out.palette);
}

static void test_city_tap_sets_iana_name(void)
{
    open_page(UI_ROW_TZ);
    nav.view.page = UI_PAGE_TZ_CITY;
    nav.view.region = "Europe";
    const char *want = ui_menu_city_at("Europe", 1);
    int y = row_y(1);
    TEST_ASSERT_TRUE(y >= 0);
    TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, tap(120, y));
    TEST_ASSERT_EQUAL_STRING(want, out.tz);
    TEST_ASSERT_TRUE(strchr(out.tz, '/') != NULL);
}

static void test_utc_region_sets_tz(void)
{
    open_page(UI_ROW_TZ);
    for (int i = 0; ui_menu_region_at(i); i++) {
        if (strcmp(ui_menu_region_at(i), "UTC")) continue;
        int y = row_y(i);
        TEST_ASSERT_TRUE(y >= 0);
        strcpy(cfg.tz, "Europe/London");
        TEST_ASSERT_EQUAL(UI_ACT_SETTINGS, tap(120, y));
        TEST_ASSERT_EQUAL_STRING("Etc/UTC", out.tz);
        TEST_ASSERT_EQUAL(UI_PAGE_TZ_REGION, nav.view.page);
        TEST_ASSERT_FALSE(dc);
        return;
    }
    TEST_FAIL_MESSAGE("UTC row not reachable");
}

static void test_drag_never_taps(void)
{
    to_menu();
    ui_nav_t before = nav;
    drag(120, 120, 3);
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    (void)before;
    open_page(UI_ROW_SOUND);
    tap(120, 5);
    nav.view.page = UI_PAGE_TZ_REGION;
    ui_act_t a = drag(120, 120, 30);
    TEST_ASSERT_NOT_EQUAL(UI_ACT_SETTINGS, a);
    TEST_ASSERT_NOT_EQUAL(UI_ACT_ROM_SWITCH, a);
    TEST_ASSERT_NOT_EQUAL(UI_PAGE_ROM, nav.view.page);
}

static void test_scroll_clamped(void)
{
    to_menu();
    nav.view.page = UI_PAGE_TZ_REGION;
    nav.view.region = NULL;
    int max = ui_menu_scroll_max(&nav.view);
    TEST_ASSERT_TRUE(max > 0);
    drag(120, 120, -30000);
    TEST_ASSERT_EQUAL(max, nav.view.scroll);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, drag(120, 120, -10));
    drag(120, 120, 30000);
    TEST_ASSERT_EQUAL(0, nav.view.scroll);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, drag(120, 120, -20));
    TEST_ASSERT_EQUAL(20, nav.view.scroll);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_transitions);
    RUN_TEST(test_pages_and_back);
    RUN_TEST(test_settings_rom_row_opens_page);
    RUN_TEST(test_tap_current_closes);
    RUN_TEST(test_tap_other_confirms);
    RUN_TEST(test_confirm_cancel_and_switch);
    RUN_TEST(test_confirm_boot_and_swipe_return_to_list);
    RUN_TEST(test_confirm_ignores_quick_second_tap);
    RUN_TEST(test_rom_back);
    RUN_TEST(test_rom_first_start);
    RUN_TEST(test_rom_current_unlisted);
    RUN_TEST(test_rom_page_empty);
    RUN_TEST(test_edit_changes_only_touched_key);
    RUN_TEST(test_mute_toggle_keeps_volume);
    RUN_TEST(test_dim_slider_snaps);
    RUN_TEST(test_drag_stays_on_its_slider);
    RUN_TEST(test_brightness_and_palette);
    RUN_TEST(test_city_tap_sets_iana_name);
    RUN_TEST(test_utc_region_sets_tz);
    RUN_TEST(test_drag_never_taps);
    RUN_TEST(test_scroll_clamped);
    RUN_TEST(test_swipe_down_on_dimming_changes_nothing);
    RUN_TEST(test_slow_swipe_up_on_sound_changes_nothing);
    RUN_TEST(test_drag_from_off_control_crossing_volume_changes_nothing);
    RUN_TEST(test_drag_started_on_slider_adjusts_it);
    RUN_TEST(test_lost_release_does_not_lock_next_drag);
    return UNITY_END();
}
