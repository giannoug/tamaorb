// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "ui.h"

#define DARK  0x0000
#define LIGHT 0xFFFF
#define SENT  0x1234

static uint16_t fb[UI_W * UI_H];
void setUp(void) { for (int i = 0; i < UI_W * UI_H; i++) fb[i] = SENT; }
void tearDown(void) {}

// A stand-in for an encoded code: finder patterns in three corners (7x7 dark ring, light ring, 3x3 dark core)
// and a checkerboard elsewhere. Encoding itself is the device's job (espressif/qrcode); ui only draws.
static void make_grid(ui_qr_t *q, int size)
{
    memset(q, 0, sizeof *q);
    q->size = (uint8_t)size;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) q->mod[y][x] = (uint8_t)((x + y) & 1);
    const int corners[3][2] = { { 0, 0 }, { size - 7, 0 }, { 0, size - 7 } };
    for (int c = 0; c < 3; c++)
        for (int j = 0; j < 7; j++)
            for (int i = 0; i < 7; i++) {
                bool ring = i == 0 || i == 6 || j == 0 || j == 6, core = i >= 2 && i <= 4 && j >= 2 && j <= 4;
                q->mod[corners[c][1] + j][corners[c][0] + i] = ring || core;
            }
}

static uint16_t px(int x, int y) { return fb[y * UI_W + x]; }

static void test_draws_within_max(void)
{
    ui_qr_t q;
    make_grid(&q, 25);
    int side = ui_draw_qr(fb, 120, 120, 200, &q, DARK, LIGHT);
    TEST_ASSERT_GREATER_THAN(0, side);
    TEST_ASSERT_LESS_OR_EQUAL(200, side);
    int x0 = 120 - side / 2, y0 = 120 - side / 2;
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++) {
            bool inside = x >= x0 && x < x0 + side && y >= y0 && y < y0 + side;
            TEST_ASSERT_EQUAL_HEX16(inside ? 1 : 0, (px(x, y) != SENT) ? 1 : 0);
        }
}

// finder at module (mx,my): 7x7 dark ring, 5x5 light ring, 3x3 dark core
static void check_finder(int x0, int y0, int m, int mx, int my)
{
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++) {
            int ring = (i == 0 || i == 6 || j == 0 || j == 6) ? 0 : (i >= 2 && i <= 4 && j >= 2 && j <= 4) ? 2 : 1;
            uint16_t want = ring == 1 ? LIGHT : DARK;
            int x = x0 + (2 + mx + i) * m + m / 2, y = y0 + (2 + my + j) * m + m / 2;
            TEST_ASSERT_EQUAL_HEX16(want, px(x, y));
        }
}

static void test_finder_patterns(void)
{
    ui_qr_t q;
    make_grid(&q, 25);
    int side = ui_draw_qr(fb, 120, 120, 200, &q, DARK, LIGHT);
    int m = side / 29;  // 25 modules + 2 * 2 quiet
    TEST_ASSERT_EQUAL(29 * m, side);
    int x0 = 120 - side / 2, y0 = 120 - side / 2;
    check_finder(x0, y0, m, 0, 0);
    check_finder(x0, y0, m, 18, 0);
    check_finder(x0, y0, m, 0, 18);
    TEST_ASSERT_EQUAL_HEX16(LIGHT, px(x0, y0));  // quiet zone corner
}

static void test_module_scale(void)
{
    ui_qr_t q;
    make_grid(&q, UI_QR_MAX);  // version 4: 33 modules + 2 * 2 quiet = 37; 104 / 37 = 2 px per module
    TEST_ASSERT_EQUAL(37 * 2, ui_draw_qr(fb, 120, 120, 104, &q, DARK, LIGHT));
}

static void assert_untouched(void)
{
    for (int i = 0; i < UI_W * UI_H; i++) TEST_ASSERT_EQUAL_HEX16(SENT, fb[i]);
}

static void test_null_empty_or_oversize_draws_nothing(void)
{
    ui_qr_t q;
    TEST_ASSERT_EQUAL(0, ui_draw_qr(fb, 120, 120, 200, NULL, DARK, LIGHT));
    make_grid(&q, 25);
    q.size = 0;
    TEST_ASSERT_EQUAL(0, ui_draw_qr(fb, 120, 120, 200, &q, DARK, LIGHT));
    make_grid(&q, 25);
    q.size = UI_QR_MAX + 1;
    TEST_ASSERT_EQUAL(0, ui_draw_qr(fb, 120, 120, 200, &q, DARK, LIGHT));
    assert_untouched();
}

static void test_too_small_box_returns_zero(void)
{
    ui_qr_t q;
    make_grid(&q, 25);
    TEST_ASSERT_EQUAL(0, ui_draw_qr(fb, 120, 120, 20, &q, DARK, LIGHT));
    assert_untouched();
}

// Partly off the bottom-left of the screen: drawn where it shows, clipped (no writes outside fb) elsewhere.
static void test_clipped_at_edge(void)
{
    ui_qr_t q;
    make_grid(&q, 25);
    int side = ui_draw_qr(fb, 10, 230, 200, &q, DARK, LIGHT);  // 29 * 6 = 174 px: x -77..96, y 143..316
    TEST_ASSERT_EQUAL(174, side);
    TEST_ASSERT_NOT_EQUAL(SENT, px(0, 229));
    TEST_ASSERT_NOT_EQUAL(SENT, px(96, 239));
    TEST_ASSERT_EQUAL_HEX16(SENT, px(97, 239));
    TEST_ASSERT_EQUAL_HEX16(SENT, px(0, 142));
    // x -77..-1 is off the left edge; without the x clip those writes would wrap onto the right end of the row
    // above (fb[py * UI_W + px] with px < 0), so the right edge must still hold the sentinel.
    for (int y = 143; y < UI_H; y++)
        for (int x = 160; x < UI_W; x++) TEST_ASSERT_EQUAL_HEX16(SENT, px(x, y));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_draws_within_max);
    RUN_TEST(test_finder_patterns);
    RUN_TEST(test_module_scale);
    RUN_TEST(test_null_empty_or_oversize_draws_nothing);
    RUN_TEST(test_too_small_box_returns_zero);
    RUN_TEST(test_clipped_at_edge);
    return UNITY_END();
}
