// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "ui.h"

static uint16_t fb[UI_W * UI_H];
static int runs[UI_H][2], nruns;
static void rec(void *ctx, const uint16_t *f, int y0, int y1)
{
    (void)ctx;
    TEST_ASSERT_EQUAL_PTR(fb, f);
    runs[nruns][0] = y0;
    runs[nruns][1] = y1;
    nruns++;
}
static ui_sent_t s;
void setUp(void)
{
    memset(fb, 0, sizeof fb);
    memset(&s, 0, sizeof s);  // stale hashes: only "valid" makes the first send whole
    ui_sent_init(&s);
    nruns = 0;
}
void tearDown(void) {}

static void test_first_frame_is_whole(void)
{
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(1, nruns);
    TEST_ASSERT_EQUAL(0, runs[0][0]);
    TEST_ASSERT_EQUAL(UI_H, runs[0][1]);
}

static void test_unchanged_frame_sends_nothing(void)
{
    ui_send_changed(&s, fb, rec, NULL);
    nruns = 0;
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(0, nruns);
}

static void test_changed_rows_only(void)
{
    ui_send_changed(&s, fb, rec, NULL);
    nruns = 0;
    fb[50 * UI_W + 239] = 1;   // last pixel of row 50: a change anywhere in the row counts
    fb[51 * UI_W] = 1;
    fb[200 * UI_W + 7] = 1;
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(2, nruns);
    TEST_ASSERT_EQUAL(50, runs[0][0]);
    TEST_ASSERT_EQUAL(52, runs[0][1]);
    TEST_ASSERT_EQUAL(200, runs[1][0]);
    TEST_ASSERT_EQUAL(201, runs[1][1]);
    nruns = 0;
    ui_send_changed(&s, fb, rec, NULL);  // now remembered
    TEST_ASSERT_EQUAL(0, nruns);
}

static void test_close_runs_merge(void)
{
    ui_send_changed(&s, fb, rec, NULL);
    nruns = 0;
    fb[10 * UI_W] = 1;
    fb[15 * UI_W] = 1;   // 4 unchanged rows between: merged
    fb[21 * UI_W] = 1;   // 5 unchanged rows between: separate
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(2, nruns);
    TEST_ASSERT_EQUAL(10, runs[0][0]);
    TEST_ASSERT_EQUAL(16, runs[0][1]);
    TEST_ASSERT_EQUAL(21, runs[1][0]);
    TEST_ASSERT_EQUAL(22, runs[1][1]);
}

static void test_last_row_and_forget(void)
{
    ui_send_changed(&s, fb, rec, NULL);
    nruns = 0;
    fb[(UI_H - 1) * UI_W + 3] = 9;
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(1, nruns);
    TEST_ASSERT_EQUAL(UI_H - 1, runs[0][0]);
    TEST_ASSERT_EQUAL(UI_H, runs[0][1]);
    nruns = 0;
    ui_sent_forget(&s);
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(1, nruns);
    TEST_ASSERT_EQUAL(0, runs[0][0]);
    TEST_ASSERT_EQUAL(UI_H, runs[0][1]);
}

// A pixel swapped with its neighbour, or two changes that cancel in a plain sum, still counts as a change.
static void test_hash_sees_swaps_and_cancelling_changes(void)
{
    for (int i = 0; i < UI_W; i++) fb[100 * UI_W + i] = (uint16_t)(i * 37);
    ui_send_changed(&s, fb, rec, NULL);
    nruns = 0;
    uint16_t t = fb[100 * UI_W + 10];
    fb[100 * UI_W + 10] = fb[100 * UI_W + 11];
    fb[100 * UI_W + 11] = t;
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(1, nruns);
    TEST_ASSERT_EQUAL(100, runs[0][0]);
    nruns = 0;
    fb[100 * UI_W + 20] += 1;
    fb[100 * UI_W + 30] -= 1;
    ui_send_changed(&s, fb, rec, NULL);
    TEST_ASSERT_EQUAL(1, nruns);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_frame_is_whole);
    RUN_TEST(test_unchanged_frame_sends_nothing);
    RUN_TEST(test_changed_rows_only);
    RUN_TEST(test_close_runs_merge);
    RUN_TEST(test_last_row_and_forget);
    RUN_TEST(test_hash_sees_swaps_and_cancelling_changes);
    return UNITY_END();
}
