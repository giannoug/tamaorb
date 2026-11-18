// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "ui.h"

static uint16_t fb[UI_W * UI_H];
static uint16_t BG, INK, GHOST;
#define PX(x, y) fb[(y) * UI_W + (x)]

void setUp(void)
{
    BG = ui_rgb565(UI_PALETTE_CLASSIC.bg);
    INK = ui_rgb565(UI_PALETTE_CLASSIC.ink);
    GHOST = ui_rgb565(ui_mix(UI_PALETTE_CLASSIC.bg, UI_PALETTE_CLASSIC.ink, UI_PALETTE_CLASSIC.ghost_pct));
}
void tearDown(void) {}

static void test_text_draws_ink(void)
{
    memset(fb, 0, sizeof fb);
    ui_draw_text(fb, 0, 0, "A", 1, INK);
    int n = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) n += PX(x, y) == INK;
    TEST_ASSERT_GREATER_OR_EQUAL(10, n);
}

static void test_text_width_excludes_trailing_column(void)
{
    TEST_ASSERT_EQUAL(15, ui_text_width("AB", 1));
    TEST_ASSERT_EQUAL(30, ui_text_width("AB\nlonger line", 2));
}

#define BAR_Y 111  // the middle of the catch-up bar (canvas CatchupSkip)

static void test_catchup_segments(void)
{
    ui_render_catchup(fb, "P2 #1", 3600, 7200, 120, UI_SKIP_BUTTON, &UI_PALETTE_CLASSIC);
    int x0 = 120 - (16 * 9 - 2) / 2;  // 49
    TEST_ASSERT_EQUAL_HEX16(INK, PX(x0 + 7 * 9 + 3, BAR_Y));    // segment 8 filled
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(x0 + 8 * 9 + 3, BAR_Y));  // segment 9 empty
    TEST_ASSERT_EQUAL_HEX16(BG, PX(x0 + 7 * 9 + 7, BAR_Y));     // 2 px gap after segment 8
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(0, 0));                  // outside the circle
}

static void test_catchup_zero_total(void)
{
    ui_render_catchup(fb, NULL, 0, 0, 0, UI_SKIP_BUTTON, &UI_PALETTE_CLASSIC);  // must not divide by zero
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(120 - 71 + 3, BAR_Y));
}

// A point inside whatever control ui_*_at reports as want, by scanning; false if none.
static bool find_cu(ui_skip_t s, ui_cu_hit_t want, int *x, int *y)
{
    for (int yy = 0; yy < UI_H; yy++)
        for (int xx = 0; xx < UI_W; xx++)
            if (ui_catchup_at(s, xx, yy) == want) { *x = xx; *y = yy; return true; }
    return false;
}
static bool find_resume(const ui_resume_view_t *v, ui_resume_hit_t want, int *x, int *y)
{
    for (int yy = 0; yy < UI_H; yy++)
        for (int xx = 0; xx < UI_W; xx++)
            if (ui_resume_at(v, xx, yy) == want) { *x = xx; *y = yy; return true; }
    return false;
}

static void test_catchup_skip_hit(void)
{
    int x, y;
    TEST_ASSERT_TRUE(find_cu(UI_SKIP_BUTTON, UI_CU_SKIP, &x, &y));
    TEST_ASSERT_FALSE(find_cu(UI_SKIP_BUTTON, UI_CU_STOP, &x, &y));
    TEST_ASSERT_EQUAL(UI_CU_NONE, ui_catchup_at(UI_SKIP_BUTTON, 120, 40));
}

static void test_catchup_confirm_hits(void)
{
    int gx, gy, sx, sy, k;
    TEST_ASSERT_TRUE(find_cu(UI_SKIP_CONFIRM, UI_CU_GO_ON, &gx, &gy));
    TEST_ASSERT_TRUE(find_cu(UI_SKIP_CONFIRM, UI_CU_STOP, &sx, &sy));
    TEST_ASSERT_TRUE(sx > gx);  // Go on left, Stop right (canvas CatchupStop)
    TEST_ASSERT_FALSE(find_cu(UI_SKIP_CONFIRM, UI_CU_SKIP, &k, &k));
}

static void test_resume_hits_two_buttons(void)
{
    ui_resume_view_t v = { "P2 #1", 3 * 86400 + 4 * 3600, 3664, NULL };
    int rx, ry, fx, fy, b;
    TEST_ASSERT_TRUE(find_resume(&v, UI_RESUME_RESUME, &rx, &ry));
    TEST_ASSERT_TRUE(find_resume(&v, UI_RESUME_FF, &fx, &fy));
    TEST_ASSERT_TRUE(fx > rx);  // Resume left, Fast-fwd right
    TEST_ASSERT_FALSE(find_resume(&v, UI_RESUME_BACK, &b, &b));
}

static void test_resume_hits_back_only_with_name(void)
{
    ui_resume_view_t v = { "P2 #1", 3 * 86400 + 4 * 3600, 3664, "P1 #1" };
    int x, y, rx, ry;
    TEST_ASSERT_TRUE(find_resume(&v, UI_RESUME_BACK, &x, &y));
    TEST_ASSERT_TRUE(find_resume(&v, UI_RESUME_RESUME, &rx, &ry));
    TEST_ASSERT_TRUE(y > ry);  // below the two pills
}

static void test_resume_and_catchup_render(void)
{
    ui_resume_view_t v = { "P2 #1", 3 * 86400 + 4 * 3600, 3664, "P1 #1" };
    ui_render_resume(fb, &v, &UI_PALETTE_CLASSIC);
    int ink = 0;
    for (int i = 0; i < UI_W * UI_H; i++) ink += fb[i] == INK;
    TEST_ASSERT_GREATER_THAN(500, ink);
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(0, 0));
    ui_render_catchup(fb, "P2 #1", 3600, 7200, 120, UI_SKIP_CONFIRM, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(0, 0));
}

// Ink outside the round screen's visible span, row by row.
static int ink_off_screen(void)
{
    int n = 0;
    for (int y = 0; y < UI_H; y++) {
        int x0 = 0, x1 = UI_W - 1;
        if (!ui_geom_span(&UI_GEOM, y, y, &x0, &x1)) x0 = UI_W, x1 = -1;
        for (int x = 0; x < UI_W; x++) n += (x < x0 || x > x1) && PX(x, y) == INK;
    }
    return n;
}

// A 16-character name (the most the panel allows) stays on the glass, and the lines that carry it keep
// their own words.
static void test_long_names_fit(void)
{
    const char *name = "WWWWWWWWWWWWWWWW";
    ui_resume_view_t v = { name, 3 * 86400, 3664, name };
    ui_render_resume(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_EQUAL(0, ink_off_screen());
    // The Back pill (y 180..203, h 24): its label stays clear of the rounded ends.
    int left = 0, right = UI_W - 1;
    while (left < UI_W && PX(left, 191) != INK) left++;
    while (right > 0 && PX(right, 191) != INK) right--;
    TEST_ASSERT_TRUE(right - left > 100);
    int on_ends = 0;
    for (int y = 188; y < 196; y++)
        for (int x = 3; x < 12; x++) on_ends += PX(left + x, y) == INK || PX(right - x, y) == INK;
    TEST_ASSERT_EQUAL(0, on_ends);
    ui_render_catchup(fb, name, 3600, 7200, 120, UI_SKIP_BUTTON, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_EQUAL(0, ink_off_screen());
    ui_render_catchup(fb, name, 3600, 7200, 120, UI_SKIP_CONFIRM, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_EQUAL(0, ink_off_screen());
}

static void test_message_centred(void)
{
    ui_render_message(fb, "X", "", &UI_PALETTE_CLASSIC);
    // Find the title row: the first row below the icon that has ink between x=100 and x=140.
    int minx = 999, maxx = -1;
    for (int y = 100; y < 125; y++)
        for (int x = 90; x < 150; x++)
            if (PX(x, y) == INK) { if (x < minx) minx = x; if (x > maxx) maxx = x; }
    TEST_ASSERT_TRUE(maxx >= 0);
    int centre2 = minx + maxx;  // twice the centre
    TEST_ASSERT_INT_WITHIN(2, 239, centre2);
}

static void test_low_battery_has_title(void)
{
    ui_render_low_battery(fb, &UI_PALETTE_CLASSIC);
    int n = 0;
    for (int y = 120; y < 145; y++)
        for (int x = 30; x < 210; x++) n += PX(x, y) == INK;
    TEST_ASSERT_GREATER_THAN(50, n);
}

static void test_fmt_away(void)
{
    char b[16];
    ui_fmt_away(39, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("39s", b);
    ui_fmt_away(60, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1m", b);
    ui_fmt_away(3 * 3600 + 5 * 60, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("3h 5m", b);
    ui_fmt_away(3 * 86400 + 4 * 3600 + 59, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("3d 4h", b);
    ui_fmt_away(86400, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1d", b);
    ui_fmt_away(3 * 86400 + 4 * 3600, true, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("3d", b);
    ui_fmt_away(0, false, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("0s", b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_draws_ink);
    RUN_TEST(test_text_width_excludes_trailing_column);
    RUN_TEST(test_catchup_segments);
    RUN_TEST(test_catchup_zero_total);
    RUN_TEST(test_catchup_skip_hit);
    RUN_TEST(test_catchup_confirm_hits);
    RUN_TEST(test_resume_hits_two_buttons);
    RUN_TEST(test_resume_hits_back_only_with_name);
    RUN_TEST(test_resume_and_catchup_render);
    RUN_TEST(test_long_names_fit);
    RUN_TEST(test_message_centred);
    RUN_TEST(test_low_battery_has_title);
    RUN_TEST(test_fmt_away);
    return UNITY_END();
}
