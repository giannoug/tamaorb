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
    ui_gevent_t ev = { .kind = k, .x = (int16_t)x, .y = (int16_t)y, .dx = (int16_t)dx, .dy = (int16_t)dy };
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

// ---- Pets, the New pet list and the first-start picker ----
static const ui_rom_row_t ROMS[3] = { { "P1", "Gen 1 - tama.bin", "tama.bin", true },
                                      { "Angel", "tamaang.bin", "tamaang.bin", true },
                                      { "Mothra", "not on the SD card", "tamamot.bin", false } };
static const ui_pet_row_t PETS[3] = { { "P1 #1", "Gen 1 - 3 yr - now", "Gen 1 - 3 yr", "running now", true },
                                      { "Angel #1", "Angel - 1 yr - 5h", "Angel - 1 yr", "away 5h 2m", true },
                                      { "P2 #1", "Gen 2 - egg - 3d", "Gen 2 - egg", "away 3d 4h", false } };

// What ui_task does when the pages open: fill the rows.
static void load_roms(int count)
{
    nav.view.roms = ROMS;
    nav.view.rom_count = count;
}
static void load_pets(int count, int current)
{
    nav.view.pets = PETS;
    nav.view.pet_count = count;
    nav.view.pet_current = current;
}
static void open_pets(bool from_pet)
{
    if (from_pet) ui_nav_open_pets(&nav, true);
    else open_page(UI_ROW_PETS);
    load_pets(3, 0);
}
static bool find_any_ctl(int ctl, int *x, int *y)
{
    for (int yy = 0; yy < UI_H; yy++)
        for (int xx = 0; xx < UI_W; xx++)
            if (ui_menu_control_at(&nav.view, xx, yy, NULL) == ctl) { *x = xx + 4; *y = yy + 4; return true; }
    return false;
}
static ui_act_t tap_ctl(int ctl)
{
    int x, y;
    TEST_ASSERT_TRUE_MESSAGE(find_any_ctl(ctl, &x, &y), "control not on this page");
    return tap(x, y);
}
// Pets list -> "+ New pet" (the row after the pets) -> the ROM list, loaded.
static void open_new_pet(void)
{
    open_pets(false);
    tap_row(3);
    load_roms(3);
}

static void test_settings_pets_row_opens_pets(void)
{
    to_menu();
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, row_y(UI_ROW_PETS)));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_PETS, nav.view.page);
    TEST_ASSERT_NULL(nav.view.pets);
    TEST_ASSERT_FALSE(nav.pets_from_pet);
    load_pets(3, 0);
    TEST_ASSERT_EQUAL(4, ui_menu_row_count(&nav.view));  // three pets and "+ New pet"
}

static void test_pets_tap_opens_sheet(void)
{
    open_pets(false);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, row_y(1)));
    TEST_ASSERT_EQUAL(UI_PAGE_PET_SHEET, nav.view.page);
    TEST_ASSERT_EQUAL(1, nav.view.pet_pick);
    TEST_ASSERT_TRUE(nav.view.pets == PETS);
}

static void test_sheet_switch_acts(void)
{
    open_pets(false);
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_PET_SWITCH, tap_ctl(UI_CTL_PET_SWITCH));
    TEST_ASSERT_EQUAL(1, nav.view.pet_pick);
}

static void test_sheet_rom_missing_has_no_switch(void)
{
    open_pets(false);
    tap(120, row_y(2));  // P2 #1: its ROM is not on the card
    int x, y;
    TEST_ASSERT_FALSE(find_any_ctl(UI_CTL_PET_SWITCH, &x, &y));
    TEST_ASSERT_TRUE(find_any_ctl(UI_CTL_PET_CANCEL, &x, &y));
    TEST_ASSERT_TRUE(find_any_ctl(UI_CTL_PET_DELETE, &x, &y));
}

static void test_sheet_running_has_no_switch_or_delete(void)
{
    open_pets(false);
    tap(120, row_y(0));
    TEST_ASSERT_EQUAL(UI_PAGE_PET_SHEET, nav.view.page);
    int x, y;
    TEST_ASSERT_FALSE(find_any_ctl(UI_CTL_PET_SWITCH, &x, &y));
    TEST_ASSERT_FALSE(find_any_ctl(UI_CTL_PET_DELETE, &x, &y));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_ctl(UI_CTL_PET_CANCEL));  // Close
    TEST_ASSERT_EQUAL(UI_PAGE_PETS, nav.view.page);
}

static void test_sheet_delete_then_keep_returns_to_sheet(void)
{
    open_pets(false);
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_ctl(UI_CTL_PET_DELETE));
    TEST_ASSERT_EQUAL(UI_PAGE_PET_DELETE, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_ctl(UI_CTL_DEL_KEEP));
    TEST_ASSERT_EQUAL(UI_PAGE_PET_SHEET, nav.view.page);
    TEST_ASSERT_EQUAL(1, nav.view.pet_pick);
    tap_ctl(UI_CTL_PET_DELETE);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_NONE, 0, 0, 0, 0, true));  // BOOT: back to the sheet too
    TEST_ASSERT_EQUAL(UI_PAGE_PET_SHEET, nav.view.page);
}

static void test_delete_confirm_acts(void)
{
    open_pets(false);
    tap(120, row_y(2));
    tap_ctl(UI_CTL_PET_DELETE);
    TEST_ASSERT_EQUAL(UI_ACT_PET_DELETE, tap_ctl(UI_CTL_DEL_DELETE));
    TEST_ASSERT_EQUAL(2, nav.view.pet_pick);
}

static void test_new_pet_row_opens_rom_list(void)
{
    open_pets(false);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, row_y(3)));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    TEST_ASSERT_NULL(nav.view.roms);
    TEST_ASSERT_FALSE(nav.view.rom_first);
    TEST_ASSERT_TRUE(nav.view.pets == PETS);  // still there for the way back
}

static void test_rom_disabled_row_ignored(void)
{
    open_new_pet();
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(120, row_y(2)));  // Mothra: not on the card
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
}

static void test_new_confirm_start_acts(void)
{
    open_new_pet();
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, row_y(1)));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM_CONFIRM, nav.view.page);
    TEST_ASSERT_EQUAL(1, nav.view.rom_pick);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_ctl(UI_CTL_NEW_CANCEL));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_PET_NEW, tap_ctl(UI_CTL_NEW_START));
    TEST_ASSERT_EQUAL(1, nav.view.rom_pick);
}

static void test_new_confirm_boot_and_swipe_return_to_list(void)
{
    open_new_pet();
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_NONE, 0, 0, 0, 0, true));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    // a drag across the Start pill starts nothing
    tap(120, row_y(1));
    int x, y;
    TEST_ASSERT_TRUE(find_any_ctl(UI_CTL_NEW_START, &x, &y));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, drag_from(x, y, x + 2, y, 0, true));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, drag_from(x, y, x + 6, y, 0, false));
    TEST_ASSERT_EQUAL(UI_PAGE_ROM_CONFIRM, nav.view.page);
}

// Review finding (kept): a confirm's pills ignore taps for UI_CONFIRM_GUARD_MS after it opens, so the second
// half of a double tap on a list row that lies where a pill appears can't act.
static ui_act_t tap_at_ms(int x, int y, uint32_t t_ms)
{
    ui_gevent_t ev = { UI_G_TAP, (int16_t)x, (int16_t)y, 0, 0, false, (int16_t)x, (int16_t)y, true };
    nav.view.t_ms = t_ms;
    return ui_nav_step(&nav, &ev, false, &cfg, &out, &dc);
}

static void test_confirm_ignores_quick_second_tap(void)
{
    static const ui_pet_row_t FOUR[4] = { { "A", "s", "m", "now", true }, { "B", "s", "m", "a", true },
                                          { "C", "s", "m", "a", true }, { "D", "s", "m", "a", true } };
    ui_nav_open_pets(&nav, true);
    nav.view.pets = FOUR;
    nav.view.pet_count = 4;
    nav.view.pet_current = 0;
    // a point that is on a list row and on the sheet's Switch pill
    int px = -1, py = -1, row = -1;
    for (int y = 0; y < UI_H && px < 0; y++)
        for (int x = 0; x < UI_W && px < 0; x++) {
            int r;
            nav.view.page = UI_PAGE_PETS;
            r = ui_menu_row_at(&nav.view, x, y);
            if (r < 1 || r > 3) continue;
            nav.view.page = UI_PAGE_PET_SHEET;
            nav.view.pet_pick = r;
            if (ui_menu_control_at(&nav.view, x, y, NULL) == UI_CTL_PET_SWITCH) { px = x; py = y; row = r; }
        }
    nav.view.page = UI_PAGE_PETS;
    TEST_ASSERT_TRUE_MESSAGE(px >= 0, "no list row lies under the Switch pill");
    const uint32_t t0 = 50000;
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_at_ms(px, py, t0));          // first tap: the sheet
    TEST_ASSERT_EQUAL(UI_PAGE_PET_SHEET, nav.view.page);
    TEST_ASSERT_EQUAL(row, nav.view.pet_pick);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap_at_ms(px, py, t0 + 250));      // the double tap's second half
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap_at_ms(px, py, t0 + UI_CONFIRM_GUARD_MS - 1));
    TEST_ASSERT_EQUAL(UI_ACT_PET_SWITCH, tap_at_ms(px, py, t0 + UI_CONFIRM_GUARD_MS));
}

static void test_pets_back(void)
{
    open_pets(false);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
    ui_nav_init(&nav);
    open_pets(true);
    TEST_ASSERT_TRUE(nav.pets_from_pet);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    open_pets(false);
    step(UI_G_NONE, 0, 0, 0, 0, true);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    // the New pet list's back returns to the Pets list; the sheet's too
    open_new_pet();
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_PAGE_PETS, nav.view.page);
    tap(120, row_y(1));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_PAGE_PETS, nav.view.page);
}

static void test_back_from_sheet_keeps_scroll(void)
{
    static const ui_pet_row_t MANY[8] = { { "A", "s", "m", "now", true }, { "B", "s", "m", "a", true },
                                          { "C", "s", "m", "a", true }, { "D", "s", "m", "a", true },
                                          { "E", "s", "m", "a", true }, { "F", "s", "m", "a", true },
                                          { "G", "s", "m", "a", true }, { "H", "s", "m", "a", true } };
    for (int how = 0; how < 3; how++) {
        ui_nav_init(&nav);
        ui_nav_open_pets(&nav, true);
        nav.view.pets = MANY;
        nav.view.pet_count = 8;
        nav.view.pet_current = 0;
        int y = row_y(7);
        TEST_ASSERT_TRUE(y >= 0);
        const int scrolled = nav.view.scroll;
        TEST_ASSERT_GREATER_THAN(0, scrolled);
        tap(120, y);
        TEST_ASSERT_EQUAL(UI_PAGE_PET_SHEET, nav.view.page);
        if (how == 0) tap_ctl(UI_CTL_PET_CANCEL);
        else if (how == 1) step(UI_G_NONE, 0, 0, 0, 0, true);
        else step(UI_G_SWIPE_DOWN, 120, 10, 0, 80, false);
        TEST_ASSERT_EQUAL(UI_PAGE_PETS, nav.view.page);
        TEST_ASSERT_EQUAL_INT_MESSAGE(scrolled, nav.view.scroll, how == 0 ? "Cancel" : how == 1 ? "BOOT" : "swipe");
    }
}

static void test_first_start_picker_unchanged(void)
{
    ui_nav_open_roms(&nav);
    nav.view.rom_first = true;
    load_roms(2);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, step(UI_G_NONE, 0, 0, 0, 0, true));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, step(UI_G_SWIPE_DOWN, 120, 0, 0, 80, false));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_ROM_SWITCH, tap(120, row_y(1)));
    TEST_ASSERT_EQUAL(1, nav.view.rom_pick);
    TEST_ASSERT_EQUAL(UI_PAGE_ROM, nav.view.page);  // no confirm page
}

static void test_pets_page_empty(void)
{
    open_page(UI_ROW_PETS);
    for (int y = 30; y < UI_H; y += 4) TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(120, y));  // not loaded yet
    TEST_ASSERT_EQUAL(UI_PAGE_PETS, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
}

static void test_edit_changes_only_touched_key(void)
{
    open_page(UI_ROW_SOUND);          // the view sees brightness 80 ...
    cfg.brightness = 80;
    ui_gevent_t none = { .kind = UI_G_NONE };
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
    TEST_ASSERT_NOT_EQUAL(UI_ACT_PET_SWITCH, a);
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

// ---- Pet clock ----
static void open_clock(void)
{
    ui_nav_init(&nav);  // from the pet, whatever the previous step left open
    open_page(UI_ROW_CLOCK);
    nav.view.clock_why = UI_CLOCK_OK;
    nav.view.pet_s = 9 * 3600 + 58 * 60;
    nav.view.ball_s = 14 * 3600 + 32 * 60 + 5;
}

static void test_clock_row_opens_page(void)
{
    open_page(UI_ROW_CLOCK);
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ROW_TZ + 1, UI_ROW_CLOCK);  // right after Time zone
}

static void test_clock_back_returns_to_list(void)
{
    open_clock();
    TEST_ASSERT_TRUE(ui_menu_back_at(&nav.view, 120, 5));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(120, 5));
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
}

static void test_clock_set_opens_confirm_with_ball_time(void)
{
    open_clock();
    int x, y;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_CLOCK_SET, &x, &y, 0));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap(x, y));
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK_CONFIRM, nav.view.page);
    TEST_ASSERT_EQUAL_INT32(14 * 3600 + 32 * 60 + 5, nav.view.confirm_s);
    nav.view.ball_s += 7;  // the clock moves on; the question stays what was asked
    TEST_ASSERT_EQUAL_INT32(14 * 3600 + 32 * 60 + 5, nav.view.confirm_s);
    TEST_ASSERT_FALSE(ui_menu_back_at(&nav.view, 120, 5));
}

static void test_clock_set_ignored_when_unavailable(void)
{
    open_clock();
    int x, y;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_CLOCK_SET, &x, &y, 0));
    static const ui_clock_why_t whys[] = { UI_CLOCK_UNSUPPORTED, UI_CLOCK_NO_TIME, UI_CLOCK_CATCHUP, UI_CLOCK_PENDING };
    for (size_t i = 0; i < sizeof whys / sizeof *whys; i++) {
        nav.view.clock_why = whys[i];
        int xx, yy;
        TEST_ASSERT_FALSE(find_ctl(UI_CTL_CLOCK_SET, &xx, &yy, 0));
        TEST_ASSERT_EQUAL(UI_ACT_NONE, tap(x, y));
        TEST_ASSERT_EQUAL(UI_PAGE_CLOCK, nav.view.page);
    }
}

// Opens the confirm at t0 and returns where its pill `ctl` is.
static void clock_confirm_at(uint32_t t0, int ctl, int *px, int *py)
{
    open_clock();
    int x, y;
    TEST_ASSERT_TRUE(find_ctl(UI_CTL_CLOCK_SET, &x, &y, 0));
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_at_ms(x, y, t0));
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK_CONFIRM, nav.view.page);
    TEST_ASSERT_TRUE(find_ctl(ctl, px, py, 0));
}

static void test_clock_confirm_guard(void)
{
    const uint32_t t0 = 70000;
    int x, y;
    clock_confirm_at(t0, UI_CTL_CLOCK_OK, &x, &y);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap_at_ms(x, y, t0 + UI_CONFIRM_GUARD_MS - 1));
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK_CONFIRM, nav.view.page);
    TEST_ASSERT_EQUAL(UI_ACT_CLOCK_SYNC, tap_at_ms(x, y, t0 + UI_CONFIRM_GUARD_MS));
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK, nav.view.page);
}

static void test_clock_confirm_cancel_and_boot(void)
{
    const uint32_t t0 = 80000;
    int x, y;
    clock_confirm_at(t0, UI_CTL_CLOCK_CANCEL, &x, &y);
    TEST_ASSERT_EQUAL(UI_ACT_NONE, tap_at_ms(x, y, t0 + 100));  // guarded too
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, tap_at_ms(x, y, t0 + UI_CONFIRM_GUARD_MS));
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK, nav.view.page);
    clock_confirm_at(t0, UI_CTL_CLOCK_CANCEL, &x, &y);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_NONE, 0, 0, 0, 0, true));  // BOOT short press
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK, nav.view.page);
    TEST_ASSERT_EQUAL(UI_SCREEN_MENU, nav.screen);
    clock_confirm_at(t0, UI_CTL_CLOCK_CANCEL, &x, &y);
    TEST_ASSERT_EQUAL(UI_ACT_REDRAW, step(UI_G_SWIPE_DOWN, 120, 10, 0, 80, false));
    TEST_ASSERT_EQUAL(UI_PAGE_CLOCK, nav.view.page);
}

static void test_clock_screen_off_lands_on_pet(void)
{
    int x, y;
    clock_confirm_at(90000, UI_CTL_CLOCK_OK, &x, &y);
    ui_nav_to_pet(&nav);
    TEST_ASSERT_EQUAL(UI_SCREEN_PET, nav.screen);
    TEST_ASSERT_EQUAL(UI_PAGE_LIST, nav.view.page);
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clock_row_opens_page);
    RUN_TEST(test_clock_back_returns_to_list);
    RUN_TEST(test_clock_set_opens_confirm_with_ball_time);
    RUN_TEST(test_clock_set_ignored_when_unavailable);
    RUN_TEST(test_clock_confirm_guard);
    RUN_TEST(test_clock_confirm_cancel_and_boot);
    RUN_TEST(test_clock_screen_off_lands_on_pet);
    RUN_TEST(test_transitions);
    RUN_TEST(test_pages_and_back);
    RUN_TEST(test_settings_pets_row_opens_pets);
    RUN_TEST(test_pets_tap_opens_sheet);
    RUN_TEST(test_sheet_switch_acts);
    RUN_TEST(test_sheet_rom_missing_has_no_switch);
    RUN_TEST(test_sheet_running_has_no_switch_or_delete);
    RUN_TEST(test_sheet_delete_then_keep_returns_to_sheet);
    RUN_TEST(test_delete_confirm_acts);
    RUN_TEST(test_new_pet_row_opens_rom_list);
    RUN_TEST(test_rom_disabled_row_ignored);
    RUN_TEST(test_new_confirm_start_acts);
    RUN_TEST(test_new_confirm_boot_and_swipe_return_to_list);
    RUN_TEST(test_confirm_ignores_quick_second_tap);
    RUN_TEST(test_pets_back);
    RUN_TEST(test_back_from_sheet_keeps_scroll);
    RUN_TEST(test_first_start_picker_unchanged);
    RUN_TEST(test_pets_page_empty);
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
