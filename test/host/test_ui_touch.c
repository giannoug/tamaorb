// SPDX-License-Identifier: GPL-3.0-or-later
#include "unity.h"
#include "ui.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t one(int x) { ui_touch_t t = { 1, { { (int16_t)x, 120 } } }; return ui_touch_buttons(&t); }

static void test_zone_edges(void)
{
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A), one(0));
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A), one(79));
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_B), one(80));
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_B), one(159));
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_C), one(160));
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_C), one(239));
}

static void test_two_fingers_a_c(void)
{
    ui_touch_t t = { 2, { { 10, 120 }, { 230, 120 } } };
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A) | TC_BTN_MASK(TC_BTN_C), ui_touch_buttons(&t));
}

static void test_two_fingers_same_zone(void)
{
    ui_touch_t t = { 2, { { 10, 100 }, { 60, 140 } } };
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A), ui_touch_buttons(&t));
}

static void test_zero_points(void)
{
    ui_touch_t t = { 0, { { 10, 100 } } };
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_buttons(&t));
}

static void test_count_clamped_to_two(void)
{
    ui_touch_t t = { 7, { { 10, 100 }, { 100, 100 } } };
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A) | TC_BTN_MASK(TC_BTN_B), ui_touch_buttons(&t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zone_edges);
    RUN_TEST(test_two_fingers_a_c);
    RUN_TEST(test_two_fingers_same_zone);
    RUN_TEST(test_zero_points);
    RUN_TEST(test_count_clamped_to_two);
    return UNITY_END();
}
