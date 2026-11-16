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

static const char *URL = "http://255.255.255.255/";  // version 2 at ECC medium: 25 modules + 2-module quiet zone each side

static uint16_t px(int x, int y) { return fb[y * UI_W + x]; }

static void test_draws_within_max(void)
{
    int side = ui_draw_qr(fb, 120, 120, 200, URL, DARK, LIGHT);
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
    int side = ui_draw_qr(fb, 120, 120, 200, URL, DARK, LIGHT);
    int m = side / 29;  // 25 modules + 2 * 2 quiet
    TEST_ASSERT_EQUAL(29 * m, side);
    int x0 = 120 - side / 2, y0 = 120 - side / 2;
    check_finder(x0, y0, m, 0, 0);
    check_finder(x0, y0, m, 18, 0);
    check_finder(x0, y0, m, 0, 18);
    TEST_ASSERT_EQUAL_HEX16(LIGHT, px(x0, y0));  // quiet zone corner
}

static void test_deterministic(void)
{
    static uint16_t a[UI_W * UI_H];
    ui_draw_qr(fb, 100, 130, 180, URL, DARK, LIGHT);
    memcpy(a, fb, sizeof a);
    setUp();
    ui_draw_qr(fb, 100, 130, 180, URL, DARK, LIGHT);
    TEST_ASSERT_EQUAL_MEMORY(a, fb, sizeof a);
}

static void test_too_long_draws_nothing(void)
{
    char big[200];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    TEST_ASSERT_EQUAL(0, ui_draw_qr(fb, 120, 120, 200, big, DARK, LIGHT));
    for (int i = 0; i < UI_W * UI_H; i++) TEST_ASSERT_EQUAL_HEX16(SENT, fb[i]);
}

static void test_too_small_box_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0, ui_draw_qr(fb, 120, 120, 20, URL, DARK, LIGHT));
    for (int i = 0; i < UI_W * UI_H; i++) TEST_ASSERT_EQUAL_HEX16(SENT, fb[i]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_draws_within_max);
    RUN_TEST(test_finder_patterns);
    RUN_TEST(test_deterministic);
    RUN_TEST(test_too_long_draws_nothing);
    RUN_TEST(test_too_small_box_returns_zero);
    return UNITY_END();
}
