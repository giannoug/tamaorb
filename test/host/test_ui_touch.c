// SPDX-License-Identifier: GPL-3.0-or-later
#include "unity.h"
#include "ui.h"

void setUp(void) {}
void tearDown(void) {}

#define A TC_BTN_MASK(TC_BTN_A)
#define B TC_BTN_MASK(TC_BTN_B)
#define C TC_BTN_MASK(TC_BTN_C)

static void test_strip_split(void)
{
    TEST_ASSERT_EQUAL_INT(TC_BTN_A, ui_button_at(91, 190));
    TEST_ASSERT_EQUAL_INT(TC_BTN_B, ui_button_at(92, 190));
    TEST_ASSERT_EQUAL_INT(TC_BTN_B, ui_button_at(147, 190));
    TEST_ASSERT_EQUAL_INT(TC_BTN_C, ui_button_at(148, 190));
    TEST_ASSERT_EQUAL_INT(-1, ui_button_at(120, 167));  // the LCD's last row
    TEST_ASSERT_EQUAL_INT(-1, ui_button_at(120, 169));
    TEST_ASSERT_EQUAL_INT(TC_BTN_B, ui_button_at(120, 170));
    TEST_ASSERT_EQUAL_INT(TC_BTN_B, ui_button_at(120, 215));
    TEST_ASSERT_EQUAL_INT(-1, ui_button_at(120, 216));
    TEST_ASSERT_EQUAL_INT(-1, ui_button_at(120, 100));  // the LCD
}

static void test_button_centres(void)
{
    // on an arc concentric with the screen (radius 85 from (120,120)): B at the bottom, A and C 56 px out
    static const int want[3][2] = { { 64, 184 }, { 120, 205 }, { 176, 184 } };
    static const int btn[3] = { TC_BTN_A, TC_BTN_B, TC_BTN_C };
    for (int i = 0; i < 3; i++) {
        int x = -1, y = -1;
        ui_button_centre(btn[i], &x, &y);
        TEST_ASSERT_EQUAL_INT(want[i][0], x);
        TEST_ASSERT_EQUAL_INT(want[i][1], y);
    }
}

static void test_whole_circles_press_own_button(void)
{
    static const int btn[3] = { TC_BTN_A, TC_BTN_B, TC_BTN_C };
    for (int i = 0; i < 3; i++) {
        int cx, cy;
        ui_button_centre(btn[i], &cx, &cy);
        for (int y = cy - UI_BTN_R; y <= cy + UI_BTN_R; y++)
            for (int x = cx - UI_BTN_R; x <= cx + UI_BTN_R; x++)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= UI_BTN_R * UI_BTN_R)
                    TEST_ASSERT_EQUAL_INT_MESSAGE(btn[i], ui_button_at(x, y), "circle pixel presses another button");
    }
}

static ui_touch_t pt(int x, int y) { return (ui_touch_t){ 1, { { (int16_t)x, (int16_t)y } } }; }

static void test_lcd_touch_never_presses(void)
{
    ui_touch_tracker_t tr = { 0 };
    const ui_touch_t up = { 0 };
    ui_touch_t t = pt(120, 100);
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_track(&tr, &t));
    t = pt(120, 200);
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_track(&tr, &t));
    t = pt(200, 200);
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_track(&tr, &t));
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_track(&tr, &up));
    t = pt(120, 200);
    TEST_ASSERT_EQUAL_HEX8(B, ui_touch_track(&tr, &t));
}

static void test_strip_press_survives_moving_up(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = pt(66, 200);
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_track(&tr, &t));
    t = pt(66, 100);  // x 66 is still A's part
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_track(&tr, &t));
    t = pt(200, 100);
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_track(&tr, &t));
}

static void test_slide_a_to_c_in_strip(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = pt(40, 200);
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_track(&tr, &t));
    t = pt(200, 205);
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_track(&tr, &t));
    t = pt(120, 200);
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_track(&tr, &t));
    t = pt(40, 200);
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_track(&tr, &t));

    ui_touch_tracker_t tc = { 0 };
    t = pt(200, 200);
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tc, &t));
    t = pt(40, 205);
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_track(&tc, &t));
    t = pt(120, 200);
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tc, &t));
    t = pt(200, 200);
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tc, &t));
}

static void test_two_points_only_strip_counts(void)
{
    ui_touch_t t = { 2, { { 40, 200 }, { 200, 100 } } };
    TEST_ASSERT_EQUAL_HEX8(A, ui_touch_buttons(&t));
    t = (ui_touch_t){ 2, { { 40, 200 }, { 200, 200 } } };
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_buttons(&t));
}

static void test_two_fingers_a_c(void)
{
    ui_touch_t t = { 2, { { 10, 200 }, { 230, 200 } } };
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A) | TC_BTN_MASK(TC_BTN_C), ui_touch_buttons(&t));
}

static void test_two_fingers_same_zone(void)
{
    ui_touch_t t = { 2, { { 10, 190 }, { 60, 210 } } };
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A), ui_touch_buttons(&t));
}

static void test_zero_points(void)
{
    ui_touch_t t = { 0, { { 10, 100 } } };
    TEST_ASSERT_EQUAL_HEX8(0, ui_touch_buttons(&t));
}

static void test_count_clamped_to_two(void)
{
    ui_touch_t t = { 7, { { 10, 200 }, { 100, 200 } } };
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A) | TC_BTN_MASK(TC_BTN_B), ui_touch_buttons(&t));
}

static ui_touch_t at(int x) { return (ui_touch_t){ 1, { { (int16_t)x, 200 } } }; }
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
    ui_touch_t t = { 2, { { 20, 175 }, { 200, 180 } } };
    TEST_ASSERT_EQUAL_HEX8(A | C, ui_touch_track(&tr, &t));
}

// Review finding: after two points, lifting one must leave only the remaining finger's zone.
static void test_track_lift_first_of_two_keeps_remaining(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(120);
    ui_touch_track(&tr, &t);                                  // B down
    t = (ui_touch_t){ 2, { { 120, 190 }, { 210, 210 } } };    // C added
    TEST_ASSERT_EQUAL_HEX8(B | C, ui_touch_track(&tr, &t));
    t = at(210);                                              // B lifted
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tr, &t));
}

static void test_track_lift_a_of_a_c_is_not_chord(void)
{
    ui_touch_tracker_t tr = { 0 };
    ui_touch_t t = at(20);
    ui_touch_track(&tr, &t);                                  // A down
    t = (ui_touch_t){ 2, { { 20, 175 }, { 210, 180 } } };      // C added
    ui_touch_track(&tr, &t);
    t = at(210);                                              // A lifted
    TEST_ASSERT_EQUAL_HEX8(C, ui_touch_track(&tr, &t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_strip_split);
    RUN_TEST(test_button_centres);
    RUN_TEST(test_whole_circles_press_own_button);
    RUN_TEST(test_lcd_touch_never_presses);
    RUN_TEST(test_strip_press_survives_moving_up);
    RUN_TEST(test_slide_a_to_c_in_strip);
    RUN_TEST(test_two_points_only_strip_counts);
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
