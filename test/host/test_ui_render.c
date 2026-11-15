// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "ui.h"

static uint16_t fb[UI_W * UI_H], fb2[UI_W * UI_H];
static tc_frame_t frame;
static uint16_t BG, INK, GHOST;

void setUp(void)
{
    memset(&frame, 0, sizeof frame);
    BG = ui_rgb565(UI_PALETTE_CLASSIC.bg);
    INK = ui_rgb565(UI_PALETTE_CLASSIC.ink);
    GHOST = ui_rgb565(ui_mix(UI_PALETTE_CLASSIC.bg, UI_PALETTE_CLASSIC.ink, UI_PALETTE_CLASSIC.ghost_pct));
}
void tearDown(void) {}

#define PX(x, y) fb[(y) * UI_W + (x)]

static void render(uint8_t held, bool hints) { ui_render_pet(fb, &frame, TC_MODEL_P1, held, hints, &UI_PALETTE_CLASSIC); }

static void test_rgb565_classic(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xB613, BG);
    TEST_ASSERT_EQUAL_HEX16(0x1923, INK);
    TEST_ASSERT_EQUAL_HEX16(0xA591, GHOST);
}

static void test_outside_circle_black(void)
{
    render(0, true);
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(239, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(239, 239));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(120, 2));
}

static void test_lit_and_ghost(void)
{
    frame.rows[0] = 1u;  // pixel (0,0)
    render(0, true);
    TEST_ASSERT_EQUAL_HEX16(INK, PX(24, 72));
    TEST_ASSERT_EQUAL_HEX16(INK, PX(28, 76));
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(30, 72));   // pixel (1,0) unlit
}

static void test_pixel_gap(void)
{
    frame.rows[0] = 0xFFFFFFFFu;
    render(0, true);
    TEST_ASSERT_EQUAL_HEX16(BG, PX(29, 72));   // gap column of pixel (0,0)
    TEST_ASSERT_EQUAL_HEX16(BG, PX(24, 77));   // gap row
}

static void test_last_pixel_position(void)
{
    frame.rows[15] = 1u << 31;  // pixel (31,15) at x=24+186, y=72+90
    render(0, true);
    TEST_ASSERT_EQUAL_HEX16(INK, PX(210, 162));
    TEST_ASSERT_EQUAL_HEX16(INK, PX(214, 166));
}

static void test_icon_lit(void)
{
    frame.icons = 1u << 7;  // Attention: bottom row, centre (183,192), top-left (171,180)
    render(0, true);
    TEST_ASSERT_EQUAL_HEX16(INK, PX(171 + 2, 180 + 2));
    TEST_ASSERT_EQUAL_HEX16(INK, PX(171 + 3, 180 + 3));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(171, 180));     // cell (0,0) is off
    frame.icons = 0;
    render(0, true);
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(171 + 2, 180 + 2));
}

static void test_p2_medicine_differs(void)
{
    frame.icons = 0xFF;
    ui_render_pet(fb, &frame, TC_MODEL_P1, 0, true, &UI_PALETTE_CLASSIC);
    ui_render_pet(fb2, &frame, TC_MODEL_P2, 0, true, &UI_PALETTE_CLASSIC);
    int diffs = 0;
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++)
            if (fb[y * UI_W + x] != fb2[y * UI_W + x]) {
                TEST_ASSERT_TRUE_MESSAGE(x >= 171 && x <= 194 && y >= 36 && y <= 59, "diff outside Medicine icon");
                diffs++;
            }
    TEST_ASSERT_GREATER_THAN(0, diffs);
}

static void test_held_zone_tint(void)
{
    render(TC_BTN_MASK(TC_BTN_C), false);
    uint16_t tint = ui_rgb565(ui_mix(UI_PALETTE_CLASSIC.bg, UI_PALETTE_CLASSIC.ink, 10));
    TEST_ASSERT_EQUAL_HEX16(tint, PX(230, 120));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(10, 120));
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(170, 72));  // LCD pixels keep their own colour
}

static void test_hints_toggle(void)
{
    uint16_t hint = ui_rgb565(ui_mix(UI_PALETTE_CLASSIC.bg, UI_PALETTE_CLASSIC.ink, 42));
    render(0, true);
    TEST_ASSERT_EQUAL_HEX16(hint, PX(79, 11));
    TEST_ASSERT_EQUAL_HEX16(hint, PX(159, 206));
    render(0, false);
    TEST_ASSERT_EQUAL_HEX16(BG, PX(79, 11));
}

static void test_palette_lookup(void)
{
    ui_rgb_t bg = ui_palette_by_id(1)->bg, ink = ui_palette_by_id(2)->ink;
    TEST_ASSERT_EQUAL_HEX8(0xC4, bg.r); TEST_ASSERT_EQUAL_HEX8(0xC6, bg.g); TEST_ASSERT_EQUAL_HEX8(0xBD, bg.b);
    TEST_ASSERT_EQUAL_HEX8(0xA9, ink.r); TEST_ASSERT_EQUAL_HEX8(0xD3, ink.g); TEST_ASSERT_EQUAL_HEX8(0x8E, ink.b);
    TEST_ASSERT_TRUE(ui_palette_by_id(9) == &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(ui_palette_by_id(-1) == &UI_PALETTE_CLASSIC);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_UINT8(11, ui_palette_by_id(i)->ghost_pct);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rgb565_classic);
    RUN_TEST(test_outside_circle_black);
    RUN_TEST(test_lit_and_ghost);
    RUN_TEST(test_pixel_gap);
    RUN_TEST(test_last_pixel_position);
    RUN_TEST(test_icon_lit);
    RUN_TEST(test_p2_medicine_differs);
    RUN_TEST(test_held_zone_tint);
    RUN_TEST(test_hints_toggle);
    RUN_TEST(test_palette_lookup);
    return UNITY_END();
}
