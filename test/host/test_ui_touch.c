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

#define A TC_BTN_MASK(TC_BTN_A)
#define B TC_BTN_MASK(TC_BTN_B)
#define C TC_BTN_MASK(TC_BTN_C)
static ui_touch_t at(int x) { return (ui_touch_t){ 1, { { (int16_t)x, 120 } } }; }
static const ui_touch_t none = { 0 };

static void test_track_keeps_start_zone_while_sliding(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(20);
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_track(&tr, &t));
    t = at(120);
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_track(&tr, &t));
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_track(&tr, &none));
}

static void test_track_slide_a_to_c_holds_both(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(20);
    ui_touch_track(&tr, &t);
    t = at(120);
    ui_touch_track(&tr, &t);
    t = at(200);
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_track(&tr, &t));
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_track(&tr, &none));
}

static void test_track_slide_c_to_a_holds_both(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(220);
    ui_touch_track(&tr, &t);
    t = at(30);
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_track(&tr, &t));
}

static void test_track_b_start_never_chords(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(120);
    ui_touch_track(&tr, &t);
    t = at(230);
    TEST_ASSERT_EQUAL_HEX8(B, ui_touch_track(&tr, &t));
}

static void test_track_new_touch_restarts(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(20);
    ui_touch_track(&tr, &t);
    ui_touch_track(&tr, &none);
    t = at(200);
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tr, &t));
}

static void test_track_real_two_points_pass_through(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = { 2, { { 20, 60 }, { 200, 180 } } };
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_track(&tr, &t));
}

// Review finding: after two points, lifting one must leave only the remaining finger's zone.
static void test_track_lift_first_of_two_keeps_remaining(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(120);
    ui_touch_track(&tr, &t);                                  // B down
    t = (ui_touch_t){ 2, { { 120, 100 }, { 210, 140 } } };    // C added
    TEST_ASSERT_EQUAL_HEX8(B | C, ui_touch_track(&tr, &t));
    t = at(210);                                              // B lifted
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tr, &t));
}

static void test_track_lift_a_of_a_c_is_not_chord(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(20);
    ui_touch_track(&tr, &t);                                  // A down
    t = (ui_touch_t){ 2, { { 20, 60 }, { 210, 180 } } };      // C added
    ui_touch_track(&tr, &t);
    t = at(210);                                              // A lifted
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tr, &t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zone_edges);
    RUN_TEST(test_two_fingers_a_c);
    RUN_TEST(test_two_fingers_same_zone);
    RUN_TEST(test_zero_points);
    RUN_TEST(test_count_clamped_to_two);
    RUN_TEST(test_track_keeps_start_zone_while_sliding);
    RUN_TEST(test_track_slide_a_to_c_holds_both);
    RUN_TEST(test_track_slide_c_to_a_holds_both);
    RUN_TEST(test_track_b_start_never_chords);
    RUN_TEST(test_track_new_touch_restarts);
    RUN_TEST(test_track_real_two_points_pass_through);
    RUN_TEST(test_track_lift_first_of_two_keeps_remaining);
    RUN_TEST(test_track_lift_a_of_a_c_is_not_chord);
    return UNITY_END();
}
