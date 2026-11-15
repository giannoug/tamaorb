// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "remote.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parse_tokens(void)
{
    rm_press_req_t r;
    rm_press_defaults(&r);
    TEST_ASSERT_EQUAL(0, r.n);
    TEST_ASSERT_EQUAL(150, r.hold_ms);
    TEST_ASSERT_EQUAL(200, r.gap_ms);
    TEST_ASSERT_EQUAL(500, r.settle_ms);
    TEST_ASSERT_TRUE(rm_press_parse(&r, "a C ac"));
    TEST_ASSERT_EQUAL(3, r.n);
    TEST_ASSERT_EQUAL(RM_STEP_A, r.steps[0]);
    TEST_ASSERT_EQUAL(RM_STEP_C, r.steps[1]);
    TEST_ASSERT_EQUAL(RM_STEP_AC, r.steps[2]);
}

static void test_parse_rejects(void)
{
    rm_press_req_t r;
    rm_press_defaults(&r);
    TEST_ASSERT_FALSE(rm_press_parse(&r, ""));
    TEST_ASSERT_FALSE(rm_press_parse(&r, "D"));
    TEST_ASSERT_FALSE(rm_press_parse(&r, "A  "));
    TEST_ASSERT_FALSE(rm_press_parse(&r, " A"));
    TEST_ASSERT_FALSE(rm_press_parse(&r, "A  B"));
    TEST_ASSERT_FALSE(rm_press_parse(&r, "AB"));
    char big[100] = "";
    for (int i = 0; i < 33; i++) strcat(big, "A ");
    big[strlen(big) - 1] = 0;
    TEST_ASSERT_FALSE(rm_press_parse(&r, big));
}

static void test_validate_limits(void)
{
    rm_press_req_t r;
    rm_press_defaults(&r);
    TEST_ASSERT_TRUE(rm_press_add(&r, "A"));
    TEST_ASSERT_NULL(rm_press_validate(&r));
    r.hold_ms = 49;
    TEST_ASSERT_EQUAL_STRING("hold_ms", rm_press_validate(&r));
    r.hold_ms = 150; r.gap_ms = 2001;
    TEST_ASSERT_EQUAL_STRING("gap_ms", rm_press_validate(&r));
    r.gap_ms = 200; r.settle_ms = 3001;
    TEST_ASSERT_EQUAL_STRING("settle_ms", rm_press_validate(&r));
    rm_press_defaults(&r);
    TEST_ASSERT_EQUAL_STRING("steps", rm_press_validate(&r));
    r.hold_ms = 300;
    for (int i = 0; i < 32; i++) TEST_ASSERT_TRUE(rm_press_add(&r, "B"));
    TEST_ASSERT_FALSE(rm_press_add(&r, "B"));
    TEST_ASSERT_EQUAL_STRING("too long", rm_press_validate(&r));
}

static rm_press_t seq_ac(void)
{
    rm_press_req_t r;
    rm_press_defaults(&r);
    rm_press_add(&r, "A");
    rm_press_add(&r, "C");
    rm_press_t p;
    rm_press_begin(&p, &r, 1000);
    return p;
}

static void test_mask_timeline(void)
{
    rm_press_t p = seq_ac();
    TEST_ASSERT_EQUAL(1, rm_press_mask(&p, 1000));
    TEST_ASSERT_EQUAL(1, rm_press_mask(&p, 1149));
    TEST_ASSERT_EQUAL(0, rm_press_mask(&p, 1150));
    TEST_ASSERT_EQUAL(0, rm_press_mask(&p, 1349));
    TEST_ASSERT_EQUAL(4, rm_press_mask(&p, 1350));
    TEST_ASSERT_EQUAL(0, rm_press_mask(&p, 1500));
    TEST_ASSERT_FALSE(rm_press_done(&p, 1999));
    TEST_ASSERT_TRUE(rm_press_done(&p, 2000));
    TEST_ASSERT_TRUE(rm_press_window(&p, 1999));
    TEST_ASSERT_FALSE(rm_press_window(&p, 2000));
}

static void test_mask_zero_after_done(void)
{
    rm_press_t p = seq_ac();
    for (uint32_t t = 2000; t < 2100; t += 7) TEST_ASSERT_EQUAL(0, rm_press_mask(&p, t));
    p.active = false;
    TEST_ASSERT_EQUAL(0, rm_press_mask(&p, 1000));
    TEST_ASSERT_FALSE(rm_press_window(&p, 1000));
}

static void test_wraparound(void)
{
    rm_press_req_t r;
    rm_press_defaults(&r);
    rm_press_add(&r, "AC");
    rm_press_t p;
    rm_press_begin(&p, &r, 0xFFFFFFF0u);
    TEST_ASSERT_EQUAL(5, rm_press_mask(&p, 10));
    TEST_ASSERT_FALSE(rm_press_done(&p, 400));
    TEST_ASSERT_TRUE(rm_press_done(&p, 0xFFFFFFF0u + 650));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_tokens);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_validate_limits);
    RUN_TEST(test_mask_timeline);
    RUN_TEST(test_mask_zero_after_done);
    RUN_TEST(test_wraparound);
    return UNITY_END();
}
