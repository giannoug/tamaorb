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

static void test_catchup_segments(void)
{
    ui_render_catchup(fb, 3600, 7200, 120, &UI_PALETTE_CLASSIC);
    int x0 = 120 - (16 * 9 - 2) / 2;  // 49
    TEST_ASSERT_EQUAL_HEX16(INK, PX(x0 + 7 * 9 + 3, 156));    // segment 8 filled
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(x0 + 8 * 9 + 3, 156));  // segment 9 empty
    TEST_ASSERT_EQUAL_HEX16(BG, PX(x0 + 7 * 9 + 7, 156));     // 2 px gap after segment 8
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(0, 0));                // outside the circle
}

static void test_catchup_zero_total(void)
{
    ui_render_catchup(fb, 0, 0, 0, &UI_PALETTE_CLASSIC);  // must not divide by zero
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(120 - 71 + 3, 156));
}

static void test_picker_selected_inverted(void)
{
    const char *names[] = { "P1", "P2" };
    const char *subs[] = { "Gen 1 - tama.bin", "Gen 2 - tamag2.bin" };
    ui_render_picker(fb, names, subs, 2, 0, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_EQUAL_HEX16(INK, PX(195, UI_PICKER_TOP + 16));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(195, UI_PICKER_TOP + UI_PICKER_STEP + 16));
    TEST_ASSERT_EQUAL_HEX16(INK, PX(UI_PICKER_X, UI_PICKER_TOP + UI_PICKER_STEP + 16));  // unselected border
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_draws_ink);
    RUN_TEST(test_text_width_excludes_trailing_column);
    RUN_TEST(test_catchup_segments);
    RUN_TEST(test_catchup_zero_total);
    RUN_TEST(test_picker_selected_inverted);
    RUN_TEST(test_message_centred);
    RUN_TEST(test_low_battery_has_title);
    return UNITY_END();
}
