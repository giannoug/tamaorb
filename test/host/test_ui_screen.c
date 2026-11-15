// SPDX-License-Identifier: GPL-3.0-or-later
#include "unity.h"
#include "ui.h"

#define TIMEOUT 30000u
#define A TC_BTN_MASK(TC_BTN_A)
#define B TC_BTN_MASK(TC_BTN_B)
#define C TC_BTN_MASK(TC_BTN_C)

static ui_screen_t s;
static uint32_t now;

void setUp(void) { now = 1000; ui_screen_init(&s, now); }
void tearDown(void) {}

static ui_screen_out_t step(uint32_t dt, uint8_t touch, bool boot, bool sound)
{
    now += dt;
    return ui_screen_update(&s, now, touch, boot, sound, TIMEOUT);
}

static void go_dark(void)
{
    ui_screen_out_t o = step(TIMEOUT, 0, false, false);
    TEST_ASSERT_FALSE(o.backlight);
}

static void test_timeout_turns_off(void)
{
    TEST_ASSERT_TRUE(step(TIMEOUT - 1, 0, false, false).backlight);
    TEST_ASSERT_FALSE(step(1, 0, false, false).backlight);
}

static void test_touch_passes_while_on(void)
{
    TEST_ASSERT_EQUAL_HEX8(B, step(10, B, false, false).buttons);
}

static void test_wake_touch_swallowed(void)
{
    go_dark();
    for (int i = 0; i < 3; i++) {
        ui_screen_out_t o = step(30, B, false, false);
        TEST_ASSERT_TRUE(o.backlight);
        TEST_ASSERT_EQUAL_HEX8(0, o.buttons);
    }
    TEST_ASSERT_EQUAL_HEX8(0, step(30, 0, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(B, step(30, B, false, false).buttons);
}

static void test_wake_touch_slide_still_swallowed(void)
{
    go_dark();
    TEST_ASSERT_EQUAL_HEX8(0, step(30, A, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(0, step(30, C, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(0, step(30, A | C, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(0, step(30, 0, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(C, step(30, C, false, false).buttons);
}

static void test_sound_wakes_without_swallow(void)
{
    go_dark();
    TEST_ASSERT_TRUE(step(30, 0, false, true).backlight);
    TEST_ASSERT_EQUAL_HEX8(A, step(30, A, false, true).buttons);
}

static void test_sound_keeps_screen_awake(void)
{
    step(10, 0, false, true);  // beep starts
    TEST_ASSERT_TRUE(step(TIMEOUT - 20, 0, false, true).backlight);
}

static void test_boot_short_toggles(void)
{
    step(10, 0, true, false);
    TEST_ASSERT_FALSE(step(200, 0, false, false).backlight);
    step(10, 0, true, false);
    TEST_ASSERT_TRUE(step(200, 0, false, false).backlight);
}

static void test_dark_screen_forwards_nothing(void)
{
    go_dark();
    TEST_ASSERT_EQUAL_HEX8(0, step(30, 0, false, false).buttons);
}

static void test_lift_one_of_two(void)
{
    TEST_ASSERT_EQUAL_HEX8(A | C, step(30, A | C, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(C, step(30, C, false, false).buttons);
}

static void test_long_press_once(void)
{
    step(10, 0, true, false);
    for (int t = 0; t < 2500; t += 100) step(100, 0, true, false);
    TEST_ASSERT_TRUE(ui_screen_long_press(&s));
    TEST_ASSERT_FALSE(ui_screen_long_press(&s));
    TEST_ASSERT_TRUE(step(100, 0, false, false).backlight);  // release doesn't toggle
    TEST_ASSERT_FALSE(ui_screen_long_press(&s));
}

static void test_short_press_not_long(void)
{
    step(10, 0, true, false);
    step(500, 0, true, false);
    step(10, 0, false, false);
    TEST_ASSERT_FALSE(ui_screen_long_press(&s));
}

static void test_resume_ignores_held_boot(void)
{
    ui_screen_resume(&s, now, true, false);
    TEST_ASSERT_TRUE(step(100, 0, true, false).backlight);
    TEST_ASSERT_TRUE(step(100, 0, false, false).backlight);  // releasing the cancel press doesn't toggle
    step(10, 0, true, false);
    TEST_ASSERT_FALSE(step(100, 0, false, false).backlight);  // the next press does
}

static void test_resume_swallows_held_touch(void)
{
    ui_screen_resume(&s, now, false, true);
    TEST_ASSERT_EQUAL_HEX8(0, step(30, B, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(0, step(30, 0, false, false).buttons);
    TEST_ASSERT_EQUAL_HEX8(B, step(30, B, false, false).buttons);
}

static void test_no_timeout_when_zero(void)
{
    now += 10 * TIMEOUT;
    TEST_ASSERT_TRUE(ui_screen_update(&s, now, 0, false, false, 0).backlight);
}

static void test_boot_still_toggles_without_timeout(void)
{
    ui_screen_update(&s, now += 10, 0, true, false, 0);
    TEST_ASSERT_FALSE(ui_screen_update(&s, now += 200, 0, false, false, 0).backlight);
    TEST_ASSERT_FALSE(ui_screen_update(&s, now += 60000, 0, false, false, 0).backlight);  // stays off by hand
}

static void test_wake_turns_screen_on(void)
{
    go_dark();
    ui_screen_wake(&s, now);
    TEST_ASSERT_TRUE(step(10, 0, false, false).backlight);
    TEST_ASSERT_EQUAL_HEX8(A, step(30, A, false, false).buttons);  // a wake is not a swallowed touch
}

static void test_dims_after_idle(void)
{
    TEST_ASSERT_FALSE(ui_screen_dimmed(&s, now + 14999, 15000));
    TEST_ASSERT_TRUE(ui_screen_dimmed(&s, now + 15000, 15000));
}

static void test_activity_undims(void)
{
    step(20000, 0, false, false);
    TEST_ASSERT_TRUE(ui_screen_dimmed(&s, now, 15000));
    step(10, B, false, false);
    TEST_ASSERT_FALSE(ui_screen_dimmed(&s, now, 15000));
    TEST_ASSERT_EQUAL_HEX8(B, step(10, B, false, false).buttons);  // a dimmed screen still takes presses
}

static void test_dark_screen_is_not_dimmed(void)
{
    go_dark();
    TEST_ASSERT_FALSE(ui_screen_dimmed(&s, now, 15000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_timeout_turns_off);
    RUN_TEST(test_touch_passes_while_on);
    RUN_TEST(test_wake_touch_swallowed);
    RUN_TEST(test_wake_touch_slide_still_swallowed);
    RUN_TEST(test_sound_wakes_without_swallow);
    RUN_TEST(test_sound_keeps_screen_awake);
    RUN_TEST(test_boot_short_toggles);
    RUN_TEST(test_dark_screen_forwards_nothing);
    RUN_TEST(test_lift_one_of_two);
    RUN_TEST(test_long_press_once);
    RUN_TEST(test_short_press_not_long);
    RUN_TEST(test_resume_ignores_held_boot);
    RUN_TEST(test_resume_swallows_held_touch);
    RUN_TEST(test_no_timeout_when_zero);
    RUN_TEST(test_boot_still_toggles_without_timeout);
    RUN_TEST(test_wake_turns_screen_on);
    RUN_TEST(test_dims_after_idle);
    RUN_TEST(test_activity_undims);
    RUN_TEST(test_dark_screen_is_not_dimmed);
    return UNITY_END();
}
