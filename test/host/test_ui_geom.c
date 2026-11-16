// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "ui.h"

static uint16_t fb[UI_W * UI_H];
void setUp(void) {}
void tearDown(void) {}

static void test_span_round_narrower_at_top(void)
{
    int a0, a1, b0, b1;
    int top = ui_geom_span(&UI_GEOM, 29, 45, &a0, &a1);
    int mid = ui_geom_span(&UI_GEOM, 112, 128, &b0, &b1);
    TEST_ASSERT_TRUE(top > 0);
    TEST_ASSERT_LESS_THAN(mid, top);
    TEST_ASSERT_EQUAL(top, a1 - a0 + 1);
    TEST_ASSERT_EQUAL(239, a0 + a1);  // symmetric about the centre
    TEST_ASSERT_EQUAL(239, b0 + b1);
}

static void test_span_rect_is_full_width(void)
{
    ui_geom_t r = { 320, 240, false };
    int x0, x1;
    TEST_ASSERT_EQUAL(320, ui_geom_span(&r, 0, 239, &x0, &x1));
    TEST_ASSERT_EQUAL(0, x0);
    TEST_ASSERT_EQUAL(319, x1);
}

static void test_safe_square(void)
{
    int x, y, w, h;
    ui_geom_safe(&UI_GEOM, &x, &y, &w, &h);
    TEST_ASSERT_EQUAL(w, h);
    TEST_ASSERT_GREATER_OR_EQUAL(168, w);
    TEST_ASSERT_INT_WITHIN(1, 120, x + w / 2);
    TEST_ASSERT_INT_WITHIN(1, 120, y + h / 2);
    ui_geom_t r = { 320, 240, false };
    ui_geom_safe(&r, &x, &y, &w, &h);
    TEST_ASSERT_EQUAL(320, w);
    TEST_ASSERT_EQUAL(240, h);
    TEST_ASSERT_EQUAL(0, x + y);
}

static void test_text_fit(void)
{
    TEST_ASSERT_EQUAL(10, ui_text_fit("Kaliningrad", 2, 162));
    TEST_ASSERT_EQUAL(3, ui_text_fit("abc", 1, 100));
    TEST_ASSERT_EQUAL(0, ui_text_fit("abc", 1, 6));
}

static void test_ellipsis_stays_inside_box(void)
{
    memset(fb, 0, sizeof fb);
    int w = ui_draw_text_ellipsis(fb, 50, 120, 20, "Kaliningrad is long", 2, 0xFFFF);
    TEST_ASSERT_LESS_OR_EQUAL(70, w);
    int ink = 0, ell = 0;
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++)
            if (fb[y * UI_W + x]) {
                ink++;
                TEST_ASSERT_TRUE(x >= 50 && x < 120);
                TEST_ASSERT_TRUE(y >= 20 && y < 36);
                if (x >= 50 + 8 * 2 * 3) ell++;
            }
    TEST_ASSERT_GREATER_THAN(0, ink);
    TEST_ASSERT_GREATER_THAN(0, ell);
}

static void test_ellipsis_short_text_untouched(void)
{
    memset(fb, 0, sizeof fb);
    uint16_t ref[UI_W * UI_H];
    ui_draw_text_ellipsis(fb, 0, 200, 0, "Hi", 1, 0xFFFF);
    memset(ref, 0, sizeof ref);
    ui_draw_text(ref, 0, 0, "Hi", 1, 0xFFFF);
    TEST_ASSERT_EQUAL_MEMORY(ref, fb, sizeof fb);
}

static void test_ellipsis_glyph_width(void)
{
    TEST_ASSERT_EQUAL(23, ui_text_width("ab" UI_ELLIPSIS, 1));
    memset(fb, 0, sizeof fb);
    ui_draw_text(fb, 0, 0, UI_ELLIPSIS, 1, 0xFFFF);
    int n = 0;
    for (int i = 0; i < UI_W * UI_H; i++) n += fb[i] != 0;
    TEST_ASSERT_EQUAL(3, n);
}

static void test_clipped_scroll(void)
{
    memset(fb, 0, sizeof fb);
    ui_draw_text_clipped(fb, 40, 80, 10, 5, "Scrolling text here", 1, 0xFFFF);
    int n = 0;
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++)
            if (fb[y * UI_W + x]) { n++; TEST_ASSERT_TRUE(x >= 40 && x < 80); }
    TEST_ASSERT_GREATER_THAN(20, n);
}

static void test_marquee(void)
{
    TEST_ASSERT_EQUAL(0, ui_marquee_offset(100, 100, 5000));
    TEST_ASSERT_EQUAL(0, ui_marquee_offset(50, 100, 5000));
    // text 130, box 100: overflow 30 px = 1000 ms of scrolling
    TEST_ASSERT_EQUAL(0, ui_marquee_offset(130, 100, 0));
    TEST_ASSERT_EQUAL(0, ui_marquee_offset(130, 100, 1000));
    TEST_ASSERT_EQUAL(15, ui_marquee_offset(130, 100, 1500));
    TEST_ASSERT_EQUAL(30, ui_marquee_offset(130, 100, 2000));
    TEST_ASSERT_EQUAL(30, ui_marquee_offset(130, 100, 2999));  // pause at the end
    TEST_ASSERT_EQUAL(15, ui_marquee_offset(130, 100, 3500));
    TEST_ASSERT_EQUAL(0, ui_marquee_offset(130, 100, 4000));
    TEST_ASSERT_EQUAL(0, ui_marquee_offset(130, 100, 4500));   // repeats: pause at 0
    TEST_ASSERT_EQUAL(15, ui_marquee_offset(130, 100, 5500));  // second cycle (period 5000)
    for (uint32_t t = 0; t < 20000; t += 37) {
        int o = ui_marquee_offset(130, 100, t);
        TEST_ASSERT_TRUE(o >= 0 && o <= 30);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_span_round_narrower_at_top);
    RUN_TEST(test_span_rect_is_full_width);
    RUN_TEST(test_safe_square);
    RUN_TEST(test_text_fit);
    RUN_TEST(test_ellipsis_stays_inside_box);
    RUN_TEST(test_ellipsis_short_text_untouched);
    RUN_TEST(test_ellipsis_glyph_width);
    RUN_TEST(test_clipped_scroll);
    RUN_TEST(test_marquee);
    return UNITY_END();
}
