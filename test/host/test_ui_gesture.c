// SPDX-License-Identifier: GPL-3.0-or-later
#include "unity.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define A TC_BTN_MASK(TC_BTN_A)
#define B TC_BTN_MASK(TC_BTN_B)
#define C TC_BTN_MASK(TC_BTN_C)
#define P1(x, y) { 1, { { (x), (y) } } }
#define P2(x1, y1, x2, y2) { 2, { { (x1), (y1) }, { (x2), (y2) } } }
#define UP { 0 }

typedef struct { const char *name; int n; ui_touch_t s[16]; } seq_t;

// Every sequence starts outside all bands (x and y in 24..215) -- except where noted, the finger may drift.
static const seq_t SEQS[] = {
    { "tap A", 3, { P1(30, 200), P1(30, 200), UP } },
    { "tap B", 3, { P1(120, 200), P1(121, 201), UP } },
    { "tap C", 3, { P1(200, 200), P1(200, 200), UP } },
    { "hold B", 6, { P1(120, 200), P1(120, 200), P1(120, 200), P1(120, 200), P1(120, 200), UP } },
    { "slide A to C", 6, { P1(30, 200), P1(70, 200), P1(120, 200), P1(170, 200), P1(220, 200), UP } },
    { "slide C to A, from just inside the band", 5, { P1(215, 200), P1(150, 200), P1(100, 200), P1(30, 200), UP } },
    { "two points A+C, just inside the bands", 4, { P2(24, 200, 215, 200), P2(24, 200, 215, 200), P2(24, 200, 215, 200), UP } },
    { "two points then one", 5, { P2(30, 200, 120, 200), P2(30, 200, 120, 200), P1(120, 200), P1(120, 200), UP } },
    { "slide A to B", 4, { P1(30, 200), P1(100, 200), P1(110, 200), UP } },
    { "one then two", 5, { P1(30, 200), P1(30, 200), P2(30, 200, 200, 200), P1(200, 200), UP } },
    { "starts below the top band, drifts up into it", 6, { P1(120, 30), P1(120, 24), P1(120, 15), P1(120, 3), P1(120, 3), UP } },
    { "starts above the bottom band, drifts down into it", 5, { P1(40, 210), P1(40, 215), P1(40, 230), P1(40, 239), UP } },
    { "drifts up through the band while sliding", 5, { P1(30, 30), P1(100, 10), P1(200, 2), P1(220, 0), UP } },
    { "rapid taps", 11, { P1(30, 200), UP, P1(120, 200), UP, P1(200, 200), UP, P1(30, 200), P1(30, 200), UP, P1(120, 200) } },
    { "rapid taps, same zone", 6, { P1(120, 200), UP, P1(120, 200), UP, P1(120, 200), UP } },
};

static void test_pet_taps_identical_to_tracker(void)
{
    for (unsigned i = 0; i < sizeof SEQS / sizeof SEQS[0]; i++) {
        ui_gesture_t g;
        memset(&g, 0, sizeof g);
        ui_touch_tracker_t tr = { 0 };
        for (int k = 0; k < SEQS[i].n; k++) {
            ui_gevent_t ev;
            uint8_t got = ui_gesture_feed(&g, UI_GMODE_PET, &SEQS[i].s[k], 30u * k, &ev);
            uint8_t want = ui_touch_track(&tr, &SEQS[i].s[k]);
            char msg[96];
            snprintf(msg, sizeof msg, "%s, sample %d", SEQS[i].name, k);
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, got, msg);
            TEST_ASSERT_EQUAL_MESSAGE(UI_G_NONE, ev.kind, msg);
        }
    }
}

static int feed(ui_gesture_t *g, ui_gmode_t m, int count, int x, int y, ui_gevent_t *ev, uint8_t *btn)
{
    ui_touch_t t = { (uint8_t)count, { { (int16_t)x, (int16_t)y }, { 0, 0 } } };
    uint8_t b = ui_gesture_feed(g, m, &t, 0, ev);
    if (btn) *btn = b;
    return ev->kind;
}

static void test_band_touch_never_presses(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    // starts in the top band, slides down out of it and across all thirds, a second finger joins, then lifts
    int xs[] = { 30, 100, 200, 200, 30 }, ys[] = { 5, 20, 40, 100, 120 };
    for (int i = 0; i < 5; i++) {
        feed(&g, UI_GMODE_PET, 1, xs[i], ys[i], &ev, &b);
        TEST_ASSERT_EQUAL_HEX8(0, b);
    }
    ui_touch_t two = P2(30, 200, 220, 200);
    TEST_ASSERT_EQUAL_HEX8(0, ui_gesture_feed(&g, UI_GMODE_PET, &two, 0, &ev));
    feed(&g, UI_GMODE_PET, 1, 100, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);  // the rest of the touch stays inert until all fingers lift
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    // the next touch, outside the bands, presses normally
    feed(&g, UI_GMODE_PET, 1, 120, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(B, b);
    // bottom band too
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 30, 235, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
}

static void test_swipe_down_from_top(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 120, 4, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 122, 30, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_SWIPE_DOWN, feed(&g, UI_GMODE_PET, 1, 125, 4 + UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_EQUAL_HEX8(0, b);
    TEST_ASSERT_EQUAL(4 + UI_SWIPE_DIST, ev.y);
    TEST_ASSERT_EQUAL(UI_SWIPE_DIST, ev.dy);
    TEST_ASSERT_EQUAL(5, ev.dx);
    // once only
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 125, 150, &ev, &b));
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
}

static void test_swipe_up_from_bottom(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_PET, 1, 120, 237, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 120, 237 - UI_SWIPE_DIST + 1, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_SWIPE_UP, feed(&g, UI_GMODE_PET, 1, 118, 237 - UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_EQUAL(-UI_SWIPE_DIST, ev.dy);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    // a swipe going the wrong way (up from the top) is nothing
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 120, 10, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 120, 0, &ev, &b));
}

static void test_band_touch_sideways_is_nothing(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_PET, 1, 10, 5, &ev, &b);
    for (int x = 40; x <= 230; x += 30) {
        TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, x, 10, &ev, &b));
        TEST_ASSERT_EQUAL_HEX8(0, b);
    }
    // a long diagonal with |dx| >= |dy| is not a swipe either
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 10, 5, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 10 + UI_SWIPE_DIST + 20, 5 + UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b));
}

static void test_band_touch_lift_early_is_nothing(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_PET, 1, 120, 5, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    feed(&g, UI_GMODE_PET, 1, 120, 20, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b));
    TEST_ASSERT_EQUAL_HEX8(0, b);
    // a quick tap in the band is not a pet press
    feed(&g, UI_GMODE_PET, 1, 120, 230, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b));
}

static void test_menu_tap_on_release_only(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 1, 100, 5, &ev, &b));  // a top-band tap still taps in menus
    TEST_ASSERT_EQUAL_HEX8(0, b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 1, 102, 6, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_TAP, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    TEST_ASSERT_EQUAL(100, ev.x);
    TEST_ASSERT_EQUAL(5, ev.y);
    TEST_ASSERT_TRUE(ev.released);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
}

static void test_menu_drag_reports_deltas_and_no_tap(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 1, 100, 100, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 1, 100, 100 + UI_TAP_SLOP - 1, &ev, &b));  // still within slop
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 100, 100 + UI_TAP_SLOP, &ev, &b));
    TEST_ASSERT_EQUAL(UI_TAP_SLOP - (UI_TAP_SLOP - 1), ev.dy);  // delta since the previous sample
    TEST_ASSERT_FALSE(ev.released);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 97, 130, &ev, &b));
    TEST_ASSERT_EQUAL(-3, ev.dx);
    TEST_ASSERT_EQUAL(130 - 100 - UI_TAP_SLOP, ev.dy);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    TEST_ASSERT_TRUE(ev.released);
    TEST_ASSERT_EQUAL(0, ev.dy);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    // a drag that returns to its start is still a drag, not a tap
    feed(&g, UI_GMODE_MENU, 1, 100, 100, &ev, &b);
    feed(&g, UI_GMODE_MENU, 1, 100, 140, &ev, &b);
    feed(&g, UI_GMODE_MENU, 1, 100, 100, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
}

static void test_menu_swipe_up_from_bottom(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_MENU, 1, 120, 237, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 120, 200, &ev, &b));  // drag reported before the threshold
    TEST_ASSERT_EQUAL(-37, ev.dy);
    TEST_ASSERT_EQUAL(UI_G_SWIPE_UP, feed(&g, UI_GMODE_MENU, 1, 121, 237 - UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_EQUAL(-UI_SWIPE_DIST, ev.dy);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 1, 121, 100, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));  // nothing follows a swipe
}

static void test_menu_swipe_down_from_top(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_MENU, 1, 120, 3, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 120, 40, &ev, &b));
    TEST_ASSERT_FALSE(ev.released);
    TEST_ASSERT_EQUAL(UI_G_SWIPE_DOWN, feed(&g, UI_GMODE_MENU, 1, 118, 3 + UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    // sideways band drag in a menu never swipes and ends as a released drag
    feed(&g, UI_GMODE_MENU, 1, 10, 3, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 200, 10, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    TEST_ASSERT_TRUE(ev.released);
}

static void test_second_finger_band_does_not_swipe(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    // pt[0] outside the bands, pt[1] in the top band: inert, and pt[0] moving down is no swipe
    ui_touch_t t = P2(30, 120, 200, 5);
    TEST_ASSERT_EQUAL_HEX8(0, ui_gesture_feed(&g, UI_GMODE_PET, &t, 0, &ev));
    ui_touch_t m = P2(30, 120 + UI_SWIPE_DIST + 10, 200, 5 + UI_SWIPE_DIST + 10);
    TEST_ASSERT_EQUAL_HEX8(0, ui_gesture_feed(&g, UI_GMODE_PET, &m, 30, &ev));
    TEST_ASSERT_EQUAL(UI_G_NONE, ev.kind);
    // pt[0] in the bottom band, pt[1] in the top band: direction comes from pt[0] only
    ui_touch_t u = UP;
    ui_gesture_feed(&g, UI_GMODE_PET, &u, 60, &ev);
    ui_touch_t s = P2(30, 237, 200, 5);
    ui_gesture_feed(&g, UI_GMODE_PET, &s, 90, &ev);
    ui_touch_t d = P2(30, 237 - UI_SWIPE_DIST, 200, 5 + UI_SWIPE_DIST);
    ui_gesture_feed(&g, UI_GMODE_PET, &d, 200, &ev);
    TEST_ASSERT_EQUAL(UI_G_SWIPE_UP, ev.kind);
}

static void test_reset_ignores_held_touch(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    ui_gesture_reset(&g);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 120, 200, &ev, &b));
    TEST_ASSERT_EQUAL_HEX8(0, b);
    feed(&g, UI_GMODE_PET, 1, 30, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 120, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(B, b);
    // menu: held touch after reset yields no tap
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    ui_gesture_reset(&g);
    feed(&g, UI_GMODE_MENU, 1, 120, 200, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    // reset with nothing held does not swallow the next touch
    ui_gesture_reset(&g);
    feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_MENU, 1, 120, 200, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_TAP, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    // a mode change mid-touch leaves the rest of that touch inert
    feed(&g, UI_GMODE_PET, 1, 30, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(A, b);
    feed(&g, UI_GMODE_MENU, 1, 30, 200, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
}

static void test_rect_geometry_bands_scale_with_smaller_side(void)
{
    TEST_ASSERT_EQUAL(24, UI_EDGE_BAND);
    TEST_ASSERT_EQUAL(60, UI_SWIPE_DIST);
    ui_geom_t tall = { 100, 400, false };
    TEST_ASSERT_EQUAL(10, UI_EDGE_BAND_FOR(&tall));
    TEST_ASSERT_EQUAL(100, UI_SWIPE_DIST_FOR(&tall));
    ui_geom_t wide = { 400, 100, false };
    TEST_ASSERT_EQUAL(10, UI_EDGE_BAND_FOR(&wide));
    ui_geom_t big = { 500, 300, false };
    TEST_ASSERT_EQUAL(30, UI_EDGE_BAND_FOR(&big));
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gesture_set_geom(&g, &big);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_PET, 1, 29, 150, &ev, &b);  // inside the 30 px side band
    TEST_ASSERT_EQUAL_HEX8(0, b);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 470, 150, &ev, &b);  // x >= 500 - 30
    TEST_ASSERT_EQUAL_HEX8(0, b);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 30, 200, &ev, &b);  // just outside it (y in the pet button strip)
    TEST_ASSERT_EQUAL_HEX8(A, b);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 469, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(C, b);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);

    memset(&g, 0, sizeof g);
    ui_gesture_set_geom(&g, &tall);
    feed(&g, UI_GMODE_PET, 1, 50, 9, &ev, &b);  // top band is 10 px, not 40
    TEST_ASSERT_EQUAL_HEX8(0, b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 50, 100, &ev, &b));
    TEST_ASSERT_EQUAL(UI_G_SWIPE_DOWN, feed(&g, UI_GMODE_PET, 1, 50, 109, &ev, &b));
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    // 400 - 10 = 390 is the bottom band: a touch from 389 can't swipe up, one from 390 can
    feed(&g, UI_GMODE_PET, 1, 50, 389, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 50, 389 - 100, &ev, &b));
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 50, 390, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    TEST_ASSERT_EQUAL(UI_G_SWIPE_UP, feed(&g, UI_GMODE_PET, 1, 50, 390 - 100, &ev, &b));
}

static void test_side_band_touch_is_inert(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    // left-edge tap and hold press nothing
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 5, 200, &ev, &b));
        TEST_ASSERT_EQUAL_HEX8(0, b);
    }
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    // right-edge swipe toward the centre: nothing, no event, whole touch inert
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 235, 200, &ev, &b));
    for (int x = 200; x >= 20; x -= 30) {
        TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, x, 200, &ev, &b));
        TEST_ASSERT_EQUAL_HEX8(0, b);
    }
    feed(&g, UI_GMODE_PET, 1, 120, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    ui_touch_t two = P2(30, 200, 220, 200);
    TEST_ASSERT_EQUAL_HEX8(0, ui_gesture_feed(&g, UI_GMODE_PET, &two, 0, &ev));
    TEST_ASSERT_EQUAL(UI_G_NONE, ev.kind);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, ev.kind);
    // a vertical-ish drag along the side band does not swipe either
    feed(&g, UI_GMODE_PET, 1, 4, 100, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_NONE, feed(&g, UI_GMODE_PET, 1, 4, 100 + UI_SWIPE_DIST + 10, &ev, &b));
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    // the next touch outside the bands presses normally
    feed(&g, UI_GMODE_PET, 1, 120, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(B, b);
}

static void test_side_band_boundary_matches_tracker(void)
{
    int w = UI_GEOM.w;
    int xs[] = { UI_EDGE_BAND, w - UI_EDGE_BAND - 1 };
    for (int i = 0; i < 2; i++) {
        ui_gesture_t g;
        memset(&g, 0, sizeof g);
        ui_touch_tracker_t tr = { 0 };
        ui_gevent_t ev;
        ui_touch_t t = P1(xs[i], 200), up = UP;
        TEST_ASSERT_EQUAL_HEX8(ui_touch_track(&tr, &t), ui_gesture_feed(&g, UI_GMODE_PET, &t, 0, &ev));
        TEST_ASSERT_NOT_EQUAL(0, ui_touch_track(&tr, &t));
        TEST_ASSERT_EQUAL_HEX8(i ? C : A, ui_gesture_feed(&g, UI_GMODE_PET, &t, 0, &ev));
        ui_gesture_feed(&g, UI_GMODE_PET, &up, 0, &ev);
    }
    // one pixel into the band is inert
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_PET, 1, UI_EDGE_BAND - 1, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, w - UI_EDGE_BAND, 200, &ev, &b);
    TEST_ASSERT_EQUAL_HEX8(0, b);
}

static void test_corner_touch_still_swipes_vertically(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_PET, 1, 5, 5, &ev, &b);  // top-left corner
    TEST_ASSERT_EQUAL_HEX8(0, b);
    TEST_ASSERT_EQUAL(UI_G_SWIPE_DOWN, feed(&g, UI_GMODE_PET, 1, 8, 5 + UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_EQUAL_HEX8(0, b);
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
    feed(&g, UI_GMODE_PET, 1, 235, 236, &ev, &b);  // bottom-right corner
    TEST_ASSERT_EQUAL(UI_G_SWIPE_UP, feed(&g, UI_GMODE_PET, 1, 232, 236 - UI_SWIPE_DIST, &ev, &b));
    feed(&g, UI_GMODE_PET, 0, 0, 0, &ev, &b);
}

static void test_menu_side_swipe_unchanged(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_MENU, 1, 5, 200, &ev, &b);
    feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_TAP, ev.kind);  // menus still get taps from the edge
}

static void test_events_carry_start_and_first(void)
{
    ui_gesture_t g;
    memset(&g, 0, sizeof g);
    ui_gevent_t ev;
    uint8_t b;
    feed(&g, UI_GMODE_MENU, 1, 100, 100, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 110, 100, &ev, &b));
    TEST_ASSERT_TRUE(ev.first);
    TEST_ASSERT_EQUAL(100, ev.sx);
    TEST_ASSERT_EQUAL(100, ev.sy);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 130, 140, &ev, &b));
    TEST_ASSERT_FALSE(ev.first);
    TEST_ASSERT_EQUAL(100, ev.sx);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    TEST_ASSERT_FALSE(ev.first);
    TEST_ASSERT_EQUAL(100, ev.sy);
    // a tap is its touch's first event and starts where it landed
    feed(&g, UI_GMODE_MENU, 1, 50, 60, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_TAP, feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b));
    TEST_ASSERT_TRUE(ev.first);
    TEST_ASSERT_EQUAL(50, ev.sx);
    TEST_ASSERT_EQUAL(60, ev.sy);
    // a band touch: drags first, then the swipe (not first), all with the band start
    feed(&g, UI_GMODE_MENU, 1, 120, 230, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_DRAG, feed(&g, UI_GMODE_MENU, 1, 120, 200, &ev, &b));
    TEST_ASSERT_TRUE(ev.first);
    TEST_ASSERT_EQUAL(230, ev.sy);
    TEST_ASSERT_EQUAL(UI_G_SWIPE_UP, feed(&g, UI_GMODE_MENU, 1, 120, 230 - UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_FALSE(ev.first);
    TEST_ASSERT_EQUAL(230, ev.sy);
    feed(&g, UI_GMODE_MENU, 0, 0, 0, &ev, &b);
    // pet mode swipe: the touch's only event
    feed(&g, UI_GMODE_PET, 1, 120, 4, &ev, &b);
    TEST_ASSERT_EQUAL(UI_G_SWIPE_DOWN, feed(&g, UI_GMODE_PET, 1, 120, 4 + UI_SWIPE_DIST, &ev, &b));
    TEST_ASSERT_TRUE(ev.first);
    TEST_ASSERT_EQUAL(4, ev.sy);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pet_taps_identical_to_tracker);
    RUN_TEST(test_band_touch_never_presses);
    RUN_TEST(test_swipe_down_from_top);
    RUN_TEST(test_swipe_up_from_bottom);
    RUN_TEST(test_band_touch_sideways_is_nothing);
    RUN_TEST(test_band_touch_lift_early_is_nothing);
    RUN_TEST(test_menu_tap_on_release_only);
    RUN_TEST(test_menu_drag_reports_deltas_and_no_tap);
    RUN_TEST(test_menu_swipe_up_from_bottom);
    RUN_TEST(test_menu_swipe_down_from_top);
    RUN_TEST(test_second_finger_band_does_not_swipe);
    RUN_TEST(test_reset_ignores_held_touch);
    RUN_TEST(test_rect_geometry_bands_scale_with_smaller_side);
    RUN_TEST(test_side_band_touch_is_inert);
    RUN_TEST(test_side_band_boundary_matches_tracker);
    RUN_TEST(test_corner_touch_still_swipes_vertically);
    RUN_TEST(test_menu_side_swipe_unchanged);
    RUN_TEST(test_events_carry_start_and_first);
    return UNITY_END();
}
