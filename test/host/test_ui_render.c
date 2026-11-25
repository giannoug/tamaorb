// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "ui.h"
#include "ui_internal.h"

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

static void render(uint8_t held) { ui_render_pet(fb, &frame, TC_MODEL_P1, held, &UI_PALETTE_CLASSIC); }

static void test_rgb565_classic(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xB613, BG);
    TEST_ASSERT_EQUAL_HEX16(0x1923, INK);
    TEST_ASSERT_EQUAL_HEX16(0xA591, GHOST);
}

static void test_outside_circle_black(void)
{
    render(0);
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(239, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, PX(239, 239));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(120, 2));
}

static void test_lit_and_ghost(void)
{
    frame.rows[0] = 1u;  // pixel (0,0)
    render(0);
    TEST_ASSERT_EQUAL_HEX16(INK, PX(24, 72));
    TEST_ASSERT_EQUAL_HEX16(INK, PX(28, 76));
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(30, 72));   // pixel (1,0) unlit
}

static void test_pixel_gap(void)
{
    frame.rows[0] = 0xFFFFFFFFu;
    render(0);
    TEST_ASSERT_EQUAL_HEX16(BG, PX(29, 72));   // gap column of pixel (0,0)
    TEST_ASSERT_EQUAL_HEX16(BG, PX(24, 77));   // gap row
}

static void test_last_pixel_position(void)
{
    frame.rows[15] = 1u << 31;  // pixel (31,15) at x=24+186, y=72+90
    render(0);
    TEST_ASSERT_EQUAL_HEX16(INK, PX(210, 162));
    TEST_ASSERT_EQUAL_HEX16(INK, PX(214, 166));
}

static void test_icon_lit(void)
{
    frame.icons = 1u << 7;  // Attention: second row, centre (174,57), top-left (162,45)
    render(0);
    TEST_ASSERT_EQUAL_HEX16(INK, PX(162 + 2, 45 + 2));
    TEST_ASSERT_EQUAL_HEX16(INK, PX(162 + 3, 45 + 3));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(162, 45));     // cell (0,0) is off
    frame.icons = 0;
    render(0);
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(162 + 2, 45 + 2));
}

static void test_p2_medicine_differs(void)
{
    frame.icons = 0xFF;
    ui_render_pet(fb, &frame, TC_MODEL_P1, 0, &UI_PALETTE_CLASSIC);
    ui_render_pet(fb2, &frame, TC_MODEL_P2, 0, &UI_PALETTE_CLASSIC);
    int diffs = 0;
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++)
            if (fb[y * UI_W + x] != fb2[y * UI_W + x]) {
                TEST_ASSERT_TRUE_MESSAGE(x >= 153 && x <= 176 && y >= 18 && y <= 41, "diff outside Medicine icon");
                diffs++;
            }
    TEST_ASSERT_GREATER_THAN(0, diffs);
}

static bool art_has_ink(const char *const *art)
{
    for (int y = 0; y < 12; y++)
        if (strchr(art[y], '#')) return true;
    return false;
}

static void test_icon_art_per_model(void)
{
    const tc_model_t models[] = { TC_MODEL_P1, TC_MODEL_P2, TC_MODEL_ANGEL, TC_MODEL_MOTHRA };
    for (size_t m = 0; m < 4; m++)
        for (int i = 0; i < 8; i++) {
            const char *const *art = ui_icon_art(models[m], i);
            TEST_ASSERT_NOT_NULL(art);
            TEST_ASSERT_TRUE_MESSAGE(art_has_ink(art), "an icon without ink");
            for (int y = 0; y < 12; y++) TEST_ASSERT_EQUAL(12, strlen(art[y]));
        }
    // Angel: P1's pictures in its own order (docs/pet-ram.md): only Play (2) and Attention (7) stay put.
    for (int i = 0; i < 8; i++) {
        bool same = ui_icon_art(TC_MODEL_ANGEL, i) == ui_icon_art(TC_MODEL_P1, i);
        TEST_ASSERT_EQUAL_MESSAGE(i == 2 || i == 7, same, "Angel icon order");
        TEST_ASSERT_TRUE_MESSAGE(ui_icon_art(TC_MODEL_MOTHRA, i) != ui_icon_art(TC_MODEL_P1, i), "Mothra icon is P1's");
    }
    TEST_ASSERT_TRUE(ui_icon_art(TC_MODEL_ANGEL, 0) == ui_icon_art(TC_MODEL_P1, 5));  // meter first
    TEST_ASSERT_TRUE(ui_icon_art(TC_MODEL_MOTHRA, 4) == ui_icon_art(TC_MODEL_P1, 6)); // the shouting mouth
    for (int i = 0; i < 8; i++) TEST_ASSERT_TRUE(ui_icon_art(TC_MODEL_UNKNOWN, i) == ui_icon_art(TC_MODEL_P1, i));
}

static void test_pet_layout(void)
{
    frame.icons = 0xFF;  // every icon ink
    render(0);
    TEST_ASSERT_EQUAL_HEX16(GHOST, PX(UI_LCD_X, UI_LCD_Y));   // first dot, unlit
    TEST_ASSERT_EQUAL_HEX16(BG, PX(UI_LCD_X, UI_LCD_Y - 1));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(UI_LCD_X, UI_LCD_Y + 96));  // just under the LCD's last dot row
    // LCD centred on the screen; both icon rows above it (Alt 4): row 1 at y 30, x 75/105/135/165;
    // row 2 at y 57, x 66/102/138/174 (24 px boxes)
    TEST_ASSERT_EQUAL_INT(UI_H / 2, UI_LCD_Y + 16 * UI_LCD_PITCH / 2);
    static const int c[8][2] = { { 75, 30 }, { 105, 30 }, { 135, 30 }, { 165, 30 },
                                 { 66, 57 }, { 102, 57 }, { 138, 57 }, { 174, 57 } };
    for (int i = 0; i < 8; i++) {
        int ink = 0;
        for (int y = c[i][1] - 12; y < c[i][1] + 12; y++)
            for (int x = c[i][0] - 12; x < c[i][0] + 12; x++) ink += PX(x, y) == INK;
        TEST_ASSERT_GREATER_THAN_MESSAGE(10, ink, "icon missing at its new place");
    }
    // nothing ink between the rows, between row 2 and the LCD, or anywhere under the LCD when no key is held
    for (int x = 0; x < UI_W; x++) {
        TEST_ASSERT_NOT_EQUAL_HEX16(INK, PX(x, 43));
        TEST_ASSERT_NOT_EQUAL_HEX16(INK, PX(x, 70));
        for (int y = UI_LCD_Y + 96; y < UI_H; y++) TEST_ASSERT_NOT_EQUAL_HEX16(INK, PX(x, y));
    }
}

static const int BTN[3] = { TC_BTN_A, TC_BTN_B, TC_BTN_C };

static int ink_near(int cx, int cy, int r)
{
    int n = 0;
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r && PX(x, y) == INK) n++;
    return n;
}

static void test_buttons_drawn(void)
{
    // idle key: a recessed well, a ghost disc whose top 2 px crescent is shadow; no letter, no outline
    render(0);
    const uint16_t shadow = ui_rgb565(ui_mix(UI_PALETTE_CLASSIC.bg, (ui_rgb_t){ 0, 0, 0 }, 22));
    TEST_ASSERT_EQUAL_INT(10, UI_BTN_R);  // 20 px across (owner's pick)
    for (int i = 0; i < 3; i++) {
        int cx, cy;
        ui_button_centre(BTN[i], &cx, &cy);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, ink_near(cx, cy, UI_BTN_R), "an idle key has no ink");
        TEST_ASSERT_EQUAL_HEX16(GHOST, PX(cx, cy));
        TEST_ASSERT_EQUAL_HEX16(GHOST, PX(cx, cy + UI_BTN_R - 1));
        TEST_ASSERT_EQUAL_HEX16(shadow, PX(cx, cy - UI_BTN_R));      // the well's top lip
        TEST_ASSERT_EQUAL_HEX16(shadow, PX(cx, cy - UI_BTN_R + 1));
        TEST_ASSERT_EQUAL_HEX16(GHOST, PX(cx, cy - UI_BTN_R + 2));
        TEST_ASSERT_EQUAL_HEX16(BG, PX(cx, cy + UI_BTN_R + 1));       // nothing under the key
        TEST_ASSERT_EQUAL_HEX16(BG, PX(cx, cy - UI_BTN_R - 1));
    }
}

static void test_night_well_lip_darker(void)
{
    // on a dark palette 22 % black is invisible; the canvas uses 40 % (#0E140F on #18201A)
    ui_render_pet(fb, &frame, TC_MODEL_P1, 0, &UI_PALETTE_NIGHT);
    const uint16_t lip = ui_rgb565(ui_mix(UI_PALETTE_NIGHT.bg, (ui_rgb_t){ 0, 0, 0 }, 40));
    for (int i = 0; i < 3; i++) {
        int cx, cy;
        ui_button_centre(BTN[i], &cx, &cy);
        TEST_ASSERT_EQUAL_HEX16(lip, PX(cx, cy - UI_BTN_R));
    }
}

static void test_held_fills(void)
{
    render(TC_BTN_MASK(TC_BTN_B));
    for (int i = 0; i < 3; i++) {
        int cx, cy;
        ui_button_centre(BTN[i], &cx, &cy);
        TEST_ASSERT_EQUAL_HEX16(BTN[i] == TC_BTN_B ? INK : GHOST, PX(cx, cy));
    }
    int bx, by;
    ui_button_centre(TC_BTN_B, &bx, &by);
    for (int y = by - (UI_BTN_R - 4); y <= by + UI_BTN_R - 4; y++)  // held: the whole face is ink
        for (int x = bx - (UI_BTN_R - 4); x <= bx + UI_BTN_R - 4; x++)
            if ((x - bx) * (x - bx) + (y - by) * (y - by) <= (UI_BTN_R - 4) * (UI_BTN_R - 4)) TEST_ASSERT_EQUAL_HEX16(INK, PX(x, y));

    render(TC_BTN_MASK(TC_BTN_A) | TC_BTN_MASK(TC_BTN_C));
    for (int i = 0; i < 3; i++) {
        int cx, cy;
        ui_button_centre(BTN[i], &cx, &cy);
        TEST_ASSERT_EQUAL_HEX16(BTN[i] == TC_BTN_B ? GHOST : INK, PX(cx, cy));
    }
}

static void test_no_old_hints(void)
{
    uint16_t hint = ui_rgb565(ui_mix(UI_PALETTE_CLASSIC.bg, UI_PALETTE_CLASSIC.ink, 42));
    render(0);
    for (int y = 0; y < UI_H; y++) {
        TEST_ASSERT_NOT_EQUAL_HEX16(hint, PX(79, y));
        TEST_ASSERT_NOT_EQUAL_HEX16(hint, PX(159, y));
    }
    uint16_t tint = ui_rgb565(ui_mix(UI_PALETTE_CLASSIC.bg, UI_PALETTE_CLASSIC.ink, 10));
    render(TC_BTN_MASK(TC_BTN_C));
    TEST_ASSERT_EQUAL_HEX16(BG, PX(230, 120));  // no held-zone tint
    TEST_ASSERT_NOT_EQUAL_HEX16(tint, PX(200, 100));
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

// ---- Glance and settings screens ----

static const ui_palette_t *const PALS[3] = { &UI_PALETTE_CLASSIC, &UI_PALETTE_NEUTRAL, &UI_PALETTE_NIGHT };
static st_settings_t cfg;
static tc_pet_t pet;
static uint16_t scratch[UI_W * UI_H];

static void sample_cfg(void)
{
    st_defaults(&cfg, 40);
    cfg.muted = true;
    cfg.dim_battery_s = 30;
    strcpy(cfg.tz, "Europe/Isle_of_Man");
}

static void sample_pet(void)
{
    pet = (tc_pet_t){ .known = true, .hunger = 1, .happy = 3, .discipline = 2, .age_years = 3, .weight = 12,
                      .droppings = 0, .sick = 0, .asleep = 0, .light_on = 1 };
}

static ui_glance_t sample_glance(void)
{
    return (ui_glance_t){ .frame = &frame, .pet = &pet, .model = TC_MODEL_P1, .clock_known = true, .hour = 14,
                          .minute = 32, .batt_pct = 80, .address = "tamaorb.local" };
}

// A 25-module stand-in for the encoded panel URL (the device encodes; ui only draws).
static const ui_qr_t *sample_qr(void)
{
    static ui_qr_t q;
    q.size = 25;
    for (int y = 0; y < 25; y++)
        for (int x = 0; x < 25; x++) q.mod[y][x] = (uint8_t)(((x * 7) ^ (y * 3)) & 1);
    return &q;
}

static ui_menu_view_t view(ui_page_t page)
{
    return (ui_menu_view_t){ .page = page, .pressed = -1, .cfg = &cfg, .region = "Europe",
                             .panel_url = "http://192.168.1.50/", .panel_qr = sample_qr(),
                             .panel_name = "tamaorb.local" };
}

// Every pixel outside the round mask is still the mask colour: nothing was drawn where the panel can't show it.
static void assert_nothing_outside_circle(const char *what)
{
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++)
            if (!ui_in_circle(x, y) && fb[y * UI_W + x] != 0x0000) {
                char msg[96];
                snprintf(msg, sizeof msg, "%s: ink outside the circle at (%d,%d)", what, x, y);
                TEST_FAIL_MESSAGE(msg);
            }
}

// Bitmap of a string as ui_draw_text draws it: scratch[y][x] != 0 where a glyph pixel is set.
static int text_bitmap(const char *s, uint8_t scale)
{
    memset(scratch, 0, sizeof scratch);
    return ui_draw_text(scratch, 0, 0, s, scale, 1);
}

// Finds the scratch bitmap (w x h) on screen: set pixels equal `color`, unset ones don't. Top-left in *ox, *oy.
static bool find_bitmap(int w, int h, uint16_t color, int *ox, int *oy)
{
    for (int y = 0; y + h <= UI_H; y++)
        for (int x = 0; x + w <= UI_W; x++) {
            bool ok = true;
            for (int j = 0; j < h && ok; j++)
                for (int i = 0; i < w && ok; i++)
                    ok = (scratch[j * UI_W + i] != 0) == (fb[(y + j) * UI_W + x + i] == color);
            if (ok) {
                if (ox) *ox = x;
                if (oy) *oy = y;
                return true;
            }
        }
    return false;
}

static bool find_text(const char *s, uint8_t scale, uint16_t color, int *ox, int *oy)
{
    int w = text_bitmap(s, scale);
    return find_bitmap(w, 8 * scale, color, ox, oy);
}

static uint16_t soft_of(const ui_palette_t *p) { return ui_rgb565(ui_mix(p->bg, p->ink, 75)); }

static void test_no_ink_outside_circle(void)
{
    sample_cfg();
    sample_pet();
    static const ui_page_t pages[] = { UI_PAGE_LIST, UI_PAGE_SOUND, UI_PAGE_BRIGHTNESS, UI_PAGE_DIMMING,
                                       UI_PAGE_PALETTE, UI_PAGE_TZ_REGION, UI_PAGE_TZ_CITY, UI_PAGE_PANEL,
                                       UI_PAGE_CLOCK, UI_PAGE_CLOCK_CONFIRM };
    char what[64];
    for (int pi = 0; pi < 3; pi++) {
        ui_glance_t g = sample_glance();
        ui_render_glance(fb, &g, PALS[pi]);
        snprintf(what, sizeof what, "glance pal %d", pi);
        assert_nothing_outside_circle(what);
        pet.asleep = 1;  // the tallest glance: "Asleep" with its second line
        ui_render_glance(fb, &g, PALS[pi]);
        assert_nothing_outside_circle(what);
        sample_pet();
        for (size_t k = 0; k < sizeof pages / sizeof pages[0]; k++) {
            ui_menu_view_t v = view(pages[k]);
            int max = ui_menu_scroll_max(&v);
            for (int s = 0; s <= max + 30; s += 7) {  // every scroll position, also past the end
                v.scroll = s;
                v.pressed = s % 3;
                for (uint32_t t = 0; t < 6000; t += 1500) {
                    v.t_ms = t;
                    ui_render_menu(fb, &v, PALS[pi]);
                    snprintf(what, sizeof what, "page %d pal %d scroll %d t %u", (int)pages[k], pi, s, (unsigned)t);
                    assert_nothing_outside_circle(what);
                }
                if (max == 0) break;
            }
        }
    }
}

// The heart shapes from the canvas (7x7, drawn at scale 2).
static const char *const HEART_FULL[7] = { ".##.##.", "#######", "#######", "#######", ".#####.", "..###..", "...#..." };
static const char *const HEART_EMPTY[7] = { ".##.##.", "#..#..#", "#.....#", "#.....#", ".#...#.", "..#.#..", "...#..." };

static bool find_shape(const char *const *rows, uint16_t color)
{
    memset(scratch, 0, sizeof scratch);
    for (int j = 0; j < 14; j++)
        for (int i = 0; i < 14; i++) scratch[j * UI_W + i] = rows[j / 2][i / 2] == '#';
    return find_bitmap(14, 14, color, NULL, NULL);
}

static void test_glance_unknown_pet_has_no_hearts(void)
{
    sample_pet();
    ui_glance_t g = sample_glance();
    ui_render_glance(fb, &g, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_shape(HEART_FULL, INK));   // hunger 1, happy 3
    TEST_ASSERT_TRUE(find_shape(HEART_EMPTY, INK));

    pet = (tc_pet_t){ .known = false, .hunger = -1, .happy = -1, .discipline = -1, .age_years = -1, .weight = -1,
                      .droppings = -1, .sick = -1, .asleep = -1, .light_on = -1 };
    ui_render_glance(fb, &g, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_FALSE(find_shape(HEART_FULL, INK));
    TEST_ASSERT_FALSE(find_shape(HEART_EMPTY, INK));
    TEST_ASSERT_TRUE(find_text("14:32", 2, INK, NULL, NULL));  // clock and address stay
    TEST_ASSERT_TRUE(find_text("tamaorb.local", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    TEST_ASSERT_FALSE(find_text("All good", 2, INK, NULL, NULL));
}

static int address_y(void)
{
    int x, y;
    TEST_ASSERT_TRUE(find_text("tamaorb.local", 1, soft_of(&UI_PALETTE_CLASSIC), &x, &y));
    return y;
}

static int clock_y(void)
{
    int x, y;
    TEST_ASSERT_TRUE(find_text("14:32", 2, INK, &x, &y));
    return y;
}

static void test_glance_closes_up_missing_rows(void)
{
    sample_pet();
    ui_glance_t g = sample_glance();
    ui_render_glance(fb, &g, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("3 yr  12 lb  disc 2/4", 1, INK, NULL, NULL));
    int full = address_y() - clock_y();

    pet.age_years = -1;  // the stats line shrinks but stays
    ui_render_glance(fb, &g, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("12 lb  disc 2/4", 1, INK, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(full, address_y() - clock_y());

    pet.weight = -1;
    pet.discipline = -1;  // no stats line at all
    ui_render_glance(fb, &g, &UI_PALETTE_CLASSIC);
    int no_stats = address_y() - clock_y();
    TEST_ASSERT_LESS_OR_EQUAL_INT(full - 8, no_stats);

    pet.hunger = -1;
    pet.happy = -1;  // no hearts row either
    ui_render_glance(fb, &g, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_LESS_OR_EQUAL_INT(no_stats - 14, address_y() - clock_y());
    TEST_ASSERT_FALSE(find_shape(HEART_EMPTY, INK));

    sample_pet();
    pet.happy = -1;      // one group of hearts left: still drawn
    pet.droppings = 1;   // a known problem keeps a need word ("All good" would need happy known)
    ui_render_glance(fb, &g, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_shape(HEART_FULL, INK));
    TEST_ASSERT_EQUAL_INT(full, address_y() - clock_y());
}

// The title strip of the pages under "< back": the tick band of a page with nothing else sliding.
static void title_band(int *y0, int *y1)
{
    ui_menu_view_t t = view(UI_PAGE_BRIGHTNESS);
    ui_menu_tick_band(&t, y0, y1);
}

static void band_copy(uint16_t *dst, int y0, int y1) { memcpy(dst, fb + y0 * UI_W, (size_t)(y1 - y0 + 1) * UI_W * 2); }

static void test_title_marquee_moves(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_BRIGHTNESS);
    int y0, y1;
    ui_menu_tick_band(&v, &y0, &y1);
    TEST_ASSERT_TRUE(y0 >= 0 && y1 < UI_H && y0 < y1);
    static uint16_t first[UI_W * UI_H];
    v.t_ms = 0;
    TEST_ASSERT_TRUE(ui_menu_needs_tick(&v));
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    band_copy(first, y0, y1);
    size_t n = (size_t)(y1 - y0 + 1) * UI_W * 2;
    v.t_ms = 1500;
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE_MESSAGE(memcmp(first, fb + y0 * UI_W, n) != 0, "title did not move");
    bool back = false;
    for (uint32_t t = 3000; t < 30000 && !back; t += 10) {
        v.t_ms = t;
        ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
        back = memcmp(first, fb + y0 * UI_W, n) == 0;
    }
    TEST_ASSERT_TRUE_MESSAGE(back, "title never came back to its start");

    v = view(UI_PAGE_SOUND);  // "Sound" fits: nothing to tick
    TEST_ASSERT_FALSE(ui_menu_needs_tick(&v));
    v = view(UI_PAGE_LIST);
    TEST_ASSERT_FALSE(ui_menu_needs_tick(&v));
}

// Scroll that brings city `name` of the view's region into the list; its row index in *row.
static int scroll_to_city(ui_menu_view_t *v, const char *name, int *row)
{
    int k = -1;
    for (int i = 0; ui_menu_city_at(v->region, i); i++)
        if (!strcmp(ui_menu_city_at(v->region, i), name)) k = i;
    TEST_ASSERT_TRUE(k >= 0);
    *row = k;
    for (v->scroll = 0; v->scroll <= ui_menu_scroll_max(v); v->scroll += 4)
        for (int y = 0; y < UI_H; y++)
            if (ui_menu_row_at(v, UI_W / 2, y) == k && ui_menu_row_at(v, UI_W / 2, y - 1) != k &&
                ui_menu_row_at(v, UI_W / 2, y + 27) == k)  // the whole row (28 px on the canvas) is in view
                return v->scroll;
    TEST_FAIL_MESSAGE("row never fully visible");
    return 0;
}

static void test_list_long_name_ellipsis(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_TZ_CITY);
    int row;
    scroll_to_city(&v, "Europe/Kaliningrad", &row);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_FALSE(find_text("Kaliningrad", 2, INK, NULL, NULL));
    char s[16];
    bool found = false;
    for (int n = 1; n < 11 && !found; n++) {
        snprintf(s, sizeof s, "%.*s" UI_ELLIPSIS, n, "Kaliningrad");
        found = find_text(s, 2, INK, NULL, NULL);
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "no 'Kalinin...' with the ellipsis glyph");

    // The current zone is never cut: its row slides instead.
    strcpy(cfg.tz, "Europe/Kaliningrad");
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    found = false;
    for (int n = 1; n < 11 && !found; n++) {
        snprintf(s, sizeof s, "%.*s" UI_ELLIPSIS, n, "Kaliningrad");
        found = find_text(s, 2, BG, NULL, NULL);
    }
    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_TRUE(ui_menu_needs_tick(&v));
    int y0, y1, t0, t1;
    ui_menu_tick_band(&v, &y0, &y1);
    title_band(&t0, &t1);
    TEST_ASSERT_TRUE(y0 <= t0 && y1 > t1);  // the moving row is inside the band a tick redraws
}

// Row bands found from pixels (scanlines that are mostly row fill), then hit-tested at their centre.
static void check_rows_hit(ui_menu_view_t *v, const ui_palette_t *p)
{
    ui_render_menu(fb, v, p);
    uint16_t ink = ui_rgb565(p->ink), ghost = ui_rgb565(ui_mix(p->bg, p->ink, p->ghost_pct));
    int lo, hi;
    title_band(&lo, &hi);
    int bands = 0, prev = -2, start = -1;
    for (int y = hi + 1; y <= UI_H; y++) {
        int fill = 0;
        for (int x = 0; y < UI_H && x < UI_W; x++) fill += fb[y * UI_W + x] == ghost || fb[y * UI_W + x] == ink;
        bool row = fill >= 60;
        if (row && start < 0) start = y;
        if (!row && start >= 0) {
            int mid = (start + y - 1) / 2;
            int hit = ui_menu_row_at(v, UI_W / 2, mid);
            char msg[80];
            snprintf(msg, sizeof msg, "page %d scroll %d band %d..%d hit %d", (int)v->page, v->scroll, start, y - 1, hit);
            TEST_ASSERT_TRUE_MESSAGE(hit >= 0, msg);
            if (prev >= 0) TEST_ASSERT_EQUAL_INT_MESSAGE(prev + 1, hit, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(-1, ui_menu_row_at(v, UI_W / 2, y), msg);  // the gap below is no row
            prev = hit;
            bands++;
            start = -1;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(3, bands);
}

static void test_hit_test_matches_render(void)
{
    sample_cfg();
    static const ui_page_t pages[] = { UI_PAGE_LIST, UI_PAGE_TZ_REGION, UI_PAGE_TZ_CITY };
    for (size_t k = 0; k < 3; k++)
        for (int s = 0; s <= 60; s += 15) {
            ui_menu_view_t v = view(pages[k]);
            v.scroll = s;
            v.pressed = 2;
            check_rows_hit(&v, &UI_PALETTE_CLASSIC);
            check_rows_hit(&v, &UI_PALETTE_NIGHT);
        }
    ui_menu_view_t v = view(UI_PAGE_LIST);
    TEST_ASSERT_EQUAL_INT(UI_ROW_COUNT, ui_menu_row_count(&v));
    TEST_ASSERT_EQUAL_INT(-1, ui_menu_row_at(&v, UI_W / 2, 2));  // the title is no row
    v = view(UI_PAGE_SOUND);
    TEST_ASSERT_EQUAL_INT(-1, ui_menu_row_at(&v, UI_W / 2, UI_H / 2));
    TEST_ASSERT_TRUE(ui_menu_back_at(&v, UI_W / 2, 10));
    TEST_ASSERT_FALSE(ui_menu_back_at(&v, UI_W / 2, UI_H / 2));
    v = view(UI_PAGE_LIST);
    TEST_ASSERT_FALSE(ui_menu_back_at(&v, UI_W / 2, 10));
}

static void test_controls_map_values(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_BRIGHTNESS);
    int val = -1, y_hit = -1;
    for (int y = 0; y < UI_H && y_hit < 0; y++)
        if (ui_menu_control_at(&v, UI_W / 2, y, &val) == UI_CTL_BRIGHTNESS) y_hit = y;
    TEST_ASSERT_TRUE(y_hit >= 0);
    TEST_ASSERT_EQUAL_INT(UI_CTL_BRIGHTNESS, ui_menu_control_at(&v, 0, y_hit, &val));
    TEST_ASSERT_EQUAL_INT(1, val);  // past the left end clamps to the minimum
    ui_menu_control_at(&v, UI_W - 1, y_hit, &val);
    TEST_ASSERT_EQUAL_INT(100, val);

    v = view(UI_PAGE_DIMMING);
    int seen = 0;
    for (int y = 0; y < UI_H; y++) {
        int c = ui_menu_control_at(&v, 0, y, &val);
        if (c == UI_CTL_DIM_USB || c == UI_CTL_DIM_BATTERY) TEST_ASSERT_EQUAL_INT(0, val);  // left end: never
        if (c == UI_CTL_OFF_BATTERY) TEST_ASSERT_EQUAL_INT(10, val);
        if (c >= 0) seen |= 1 << c;
        c = ui_menu_control_at(&v, UI_W - 1, y, &val);
        if (c == UI_CTL_DIM_USB || c == UI_CTL_DIM_BATTERY || c == UI_CTL_OFF_BATTERY) TEST_ASSERT_EQUAL_INT(3600, val);
        if (c == UI_CTL_DIM_LEVEL) TEST_ASSERT_EQUAL_INT(100, val);
    }
    TEST_ASSERT_EQUAL_INT((1 << UI_CTL_DIM_LEVEL) | (1 << UI_CTL_DIM_USB) | (1 << UI_CTL_DIM_BATTERY) | (1 << UI_CTL_OFF_BATTERY), seen);

    v = view(UI_PAGE_SOUND);  // muted: tapping the switch maps to 0
    bool mute = false;
    for (int y = 0; y < UI_H && !mute; y++)
        if (ui_menu_control_at(&v, UI_W / 2, y, &val) == UI_CTL_MUTE) { mute = true; TEST_ASSERT_EQUAL_INT(0, val); }
    TEST_ASSERT_TRUE(mute);

    v = view(UI_PAGE_PALETTE);
    seen = 0;
    for (int y = 0; y < UI_H; y += 2)
        for (int x = 0; x < UI_W; x += 2)
            if (ui_menu_control_at(&v, x, y, &val) == UI_CTL_PALETTE) seen |= 1 << val;
    TEST_ASSERT_EQUAL_INT(7, seen);
}

static void test_panel_no_wifi_message(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_PANEL);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("192.168.1.50", 1, INK, NULL, NULL));
    TEST_ASSERT_FALSE(find_text("Wi-Fi not connected", 1, INK, NULL, NULL));
    v.panel_url = NULL;
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("Wi-Fi not connected", 1, INK, NULL, NULL));
    for (int i = 0; i < UI_W * UI_H; i++)
        TEST_ASSERT_TRUE(fb[i] == 0 || fb[i] == BG || fb[i] == INK || fb[i] == soft_of(&UI_PALETTE_CLASSIC));  // no QR
}

// No encoded QR for a URL (encoding failed, or not yet encoded): the address and name still show, no QR plate.
static void test_panel_without_qr_shows_address_only(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_PANEL);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    uint16_t plate = ui_rgb565((ui_rgb_t){ 0xF4, 0xF4, 0xEC });
    bool seen = false;
    for (int i = 0; i < UI_W * UI_H && !seen; i++) seen = fb[i] == plate;
    TEST_ASSERT_TRUE_MESSAGE(seen, "with a grid the plate is drawn");
    v.panel_qr = NULL;
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("192.168.1.50", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("tamaorb.local", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    for (int i = 0; i < UI_W * UI_H; i++) TEST_ASSERT_NOT_EQUAL(plate, fb[i]);
}

static void test_settings_list_values(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_LIST);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("muted", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Sound", 2, INK, NULL, NULL));
    cfg.muted = false;
    cfg.volume = 7;
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("7 %", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("after 30 s", 1, INK, NULL, NULL));
    v.scroll = ui_menu_scroll_max(&v) - (38 + 4);  // one row above the bottom (Factory reset is last)
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("Isle of Man", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("QR code", 1, INK, NULL, NULL));
    strcpy(cfg.tz, "EET-2EEST,M3.5.0/3,M10.5.0/4");
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("Custom", 1, INK, NULL, NULL));
}

static void test_regions_sorted_and_cities_split(void)
{
    const char *prev = NULL;
    int n = 0;
    for (const char *r; (r = ui_menu_region_at(n)); n++) {
        if (prev) TEST_ASSERT_TRUE(strcmp(prev, r) < 0);
        prev = r;
    }
    TEST_ASSERT_EQUAL_STRING("UTC", prev);
    TEST_ASSERT_EQUAL_INT(11, n);
    bool iom = false;
    int k = 0;
    for (const char *c; (c = ui_menu_city_at("Europe", k)); k++) {
        TEST_ASSERT_EQUAL_INT(0, strncmp(c, "Europe/", 7));
        iom |= !strcmp(c, "Europe/Isle_of_Man");
    }
    TEST_ASSERT_TRUE(iom);
    TEST_ASSERT_EQUAL_INT(60, k);
    TEST_ASSERT_NULL(ui_menu_city_at("Europe", -1));
    TEST_ASSERT_NULL(ui_menu_city_at("Nowhere", 0));
}

static void test_dimming_label_line_is_no_control(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_DIMMING);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    static const char *const labels[4] = { "Dim level", "Dim on USB", "Dim on batt", "Off on batt" };
    static const int ctls[4] = { UI_CTL_DIM_LEVEL, UI_CTL_DIM_USB, UI_CTL_DIM_BATTERY, UI_CTL_OFF_BATTERY };
    for (int r = 0; r < 4; r++) {
        int lx, ly;
        TEST_ASSERT_TRUE_MESSAGE(find_text(labels[r], 1, INK, &lx, &ly), labels[r]);
        int w = ui_text_width(labels[r], 1), val;
        for (int y = ly; y < ly + 8; y++) {
            for (int x = lx; x < lx + w; x++) TEST_ASSERT_EQUAL_INT_MESSAGE(-1, ui_menu_control_at(&v, x, y, &val), labels[r]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(-1, ui_menu_control_at(&v, UI_W / 2, y, &val), labels[r]);
        }
        bool track = false;  // the track below the label still takes the slider
        for (int y = ly + 8; y < ly + 33 && !track; y++) track = ui_menu_control_at(&v, UI_W / 2, y, &val) == ctls[r];
        TEST_ASSERT_TRUE_MESSAGE(track, labels[r]);
    }
}

static void test_long_centred_text_is_ellipsized(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_PANEL);
    v.panel_name = "tamaorb-abcdefghijklmnopqrstuvwx";  // 32 chars: wider than the screen at scale 1
    TEST_ASSERT_EQUAL_size_t(32, strlen(v.panel_name));
    for (int pi = 0; pi < 3; pi++) {
        ui_render_menu(fb, &v, PALS[pi]);
        assert_nothing_outside_circle("panel with a 32-char name");
        TEST_ASSERT_TRUE_MESSAGE(find_text(UI_ELLIPSIS, 1, soft_of(PALS[pi]), NULL, NULL), "no ellipsis glyph");
        TEST_ASSERT_TRUE(find_text("tamaorb-", 1, soft_of(PALS[pi]), NULL, NULL));
    }
}

static void test_hit_test_matches_render_deep_scroll(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_TZ_CITY);
    v.region = "America";  // the longest region
    int max = ui_menu_scroll_max(&v);
    TEST_ASSERT_TRUE(max > 400);
    for (int s = max / 3; s <= max + 20; s += max / 3) {
        v.scroll = s;
        v.pressed = -1;
        check_rows_hit(&v, &UI_PALETTE_CLASSIC);
    }
}

// ---- Pets, New pet and the first-start picker (canvas rows "Pets"; PickerFirst) ----
static const ui_rom_row_t ROMS[3] = { { "P1", "Gen 1 - tama.bin", "tama.bin", true },
                                      { "Angel", "tamaang.bin", "tamaang.bin", true },
                                      { "Mothra", "not on the SD card", "tamamot.bin", false } };
static const ui_pet_row_t PETS[3] = { { "P1 #1", "Gen 1 - 3 yr - now", "Gen 1 - 3 yr", "running now", true },
                                      { "Angel #1", "Angel - 1 yr - 5h", "Angel - 1 yr", "away 5h 2m", true },
                                      { "P2 #1", "Gen 2 - egg - 3d", "Gen 2 - egg", "away 3d 4h", false } };

static ui_menu_view_t rom_view(ui_page_t page, int count, bool first)
{
    sample_cfg();
    ui_menu_view_t v = view(page);
    v.roms = ROMS;
    v.rom_count = count;
    v.rom_pick = 1;
    v.rom_first = first;
    v.new_name = "Angel #2";
    v.running_name = "P1 #1";
    return v;
}

static ui_menu_view_t pet_view(ui_page_t page, int pick)
{
    sample_cfg();
    ui_menu_view_t v = view(page);
    v.pets = PETS;
    v.pet_count = 3;
    v.pet_current = 0;
    v.pet_pick = pick;
    return v;
}

static void test_pets_list_render(void)
{
    ui_menu_view_t v = pet_view(UI_PAGE_PETS, -1);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("< back", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Pets", 2, INK, NULL, NULL));
    int x, y1, y2;
    TEST_ASSERT_TRUE(find_text("P1 #1", 2, BG, &x, &y1));        // the running pet: filled, text in bg
    TEST_ASSERT_TRUE(find_text("Gen 1 - 3 yr - now", 1, BG, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Angel #1", 2, INK, &x, &y2));
    TEST_ASSERT_TRUE(y2 > y1);
    TEST_ASSERT_TRUE(find_text("+ New pet", 1, INK, NULL, NULL));
    assert_nothing_outside_circle("pets list");
}

static void test_pet_sheet_render(void)
{
    ui_menu_view_t v = pet_view(UI_PAGE_PET_SHEET, 1);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("Angel #1", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Angel - 1 yr", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("away 5h 2m", 1, INK, NULL, NULL));
    int cx, cy, sx, sy, dx, dy;
    TEST_ASSERT_TRUE(find_text("Cancel", 1, INK, &cx, &cy));
    TEST_ASSERT_TRUE(find_text("Switch", 1, BG, &sx, &sy));      // filled
    TEST_ASSERT_TRUE(find_text("Delete...", 1, INK, &dx, &dy));
    TEST_ASSERT_EQUAL(UI_CTL_PET_CANCEL, ui_menu_control_at(&v, cx + 4, cy + 4, NULL));
    TEST_ASSERT_EQUAL(UI_CTL_PET_SWITCH, ui_menu_control_at(&v, sx + 4, sy + 4, NULL));
    TEST_ASSERT_EQUAL(UI_CTL_PET_DELETE, ui_menu_control_at(&v, dx + 4, dy + 4, NULL));
    assert_nothing_outside_circle("pet sheet");
    v.pet_pick = 2;  // its ROM isn't on the card
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("ROM not on", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    TEST_ASSERT_FALSE(find_text("Switch", 1, BG, NULL, NULL));
}

static void test_pet_sheet_running_render(void)
{
    ui_menu_view_t v = pet_view(UI_PAGE_PET_SHEET, 0);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("running now", 1, INK, NULL, NULL));
    int x, y;
    TEST_ASSERT_TRUE(find_text("Close", 1, BG, &x, &y));
    TEST_ASSERT_EQUAL(UI_CTL_PET_CANCEL, ui_menu_control_at(&v, x + 4, y + 4, NULL));
    TEST_ASSERT_FALSE(find_text("Delete...", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("to delete it", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
}

// Review finding (self): with the bar part-filled, the label must show on both sides of the fill edge.
static void test_reset_hold_render(void)
{
    sample_cfg();
    ui_menu_view_t v = view(UI_PAGE_RESET);
    v.hold_pct = 60;
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    int edge = (UI_W - 172) / 2 + 2 + (172 - 4) * 60 / 100;  // the fill edge (canvas Reset: 172 px pill)
    int ink_right = 0, bg_left = 0;
    for (int y = 145 + 14; y < 145 + 22; y++) {
        for (int x = edge + 1; x < edge + 40; x++) ink_right += PX(x, y) == INK;
        for (int x = edge - 40; x < edge; x++) bg_left += PX(x, y) == BG;
    }
    TEST_ASSERT_GREATER_THAN(5, ink_right);  // "ding" in ink on the empty part
    TEST_ASSERT_GREATER_THAN(5, bg_left);    // "Keep hol" in bg on the filled part
    int x, y;
    TEST_ASSERT_TRUE(find_text("lift to cancel", 1, soft_of(&UI_PALETTE_CLASSIC), &x, &y));
    TEST_ASSERT_EQUAL(UI_CTL_RESET_HOLD, ui_menu_control_at(&v, UI_W / 2, 160, NULL));
    assert_nothing_outside_circle("reset");
}

static void test_pet_delete_render(void)
{
    ui_menu_view_t v = pet_view(UI_PAGE_PET_DELETE, 1);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("Delete", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Angel #1?", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Its save is gone", 1, INK, NULL, NULL));
    int kx, ky, dx, dy;
    TEST_ASSERT_TRUE(find_text("Keep", 1, BG, &kx, &ky));        // the safe choice is the filled one
    TEST_ASSERT_TRUE(find_text("Delete", 1, INK, &dx, &dy));
    TEST_ASSERT_EQUAL(UI_CTL_DEL_KEEP, ui_menu_control_at(&v, kx + 4, ky + 4, NULL));
    TEST_ASSERT_EQUAL(UI_CTL_DEL_DELETE, ui_menu_control_at(&v, dx + 4, dy + 4, NULL));
    TEST_ASSERT_FALSE(find_text("< back", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
}

static void test_new_pet_list_render(void)
{
    ui_menu_view_t v = rom_view(UI_PAGE_ROM, 3, false);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("New pet", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("P1", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Gen 1 - tama.bin", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Mothra", 2, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));  // greyed
    TEST_ASSERT_TRUE(find_text("not on the SD card", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    assert_nothing_outside_circle("new pet");
}

static void test_new_confirm_render(void)
{
    ui_menu_view_t v = rom_view(UI_PAGE_ROM_CONFIRM, 3, false);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("New egg?", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Angel #2 hatches", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("from tamaang.bin.", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("P1 #1 is saved and", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("The ball restarts.", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    int cx, cy, sx, sy;
    TEST_ASSERT_TRUE(find_text("Cancel", 1, INK, &cx, &cy));
    TEST_ASSERT_TRUE(find_text("Start", 1, BG, &sx, &sy));
    TEST_ASSERT_EQUAL(UI_CTL_NEW_CANCEL, ui_menu_control_at(&v, cx + 4, cy + 4, NULL));
    TEST_ASSERT_EQUAL(UI_CTL_NEW_START, ui_menu_control_at(&v, sx + 4, sy + 4, NULL));
    assert_nothing_outside_circle("new confirm");
}

static void test_rom_first_render(void)
{
    ui_menu_view_t v = rom_view(UI_PAGE_ROM, 2, true);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("Pick a pet", 2, INK, NULL, NULL));
    TEST_ASSERT_FALSE(find_text("< back", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    TEST_ASSERT_TRUE(find_text("P1", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Angel", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("tap one to start", 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL));
    TEST_ASSERT_FALSE(ui_menu_back_at(&v, 120, 5));
}

static void test_rom_page_empty_render(void)
{
    ui_menu_view_t v = rom_view(UI_PAGE_ROM, 0, false);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("No ROMs on the SD card", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(ui_menu_back_at(&v, 120, 5));
}

static void test_rom_long_sub_ellipsis(void)
{
    static const ui_rom_row_t LONG[1] = { { "P2", "Gen 2 - averyveryverylongname.bin", "x.bin", true } };
    ui_menu_view_t v = rom_view(UI_PAGE_ROM, 1, false);
    v.roms = LONG;
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_FALSE(find_text(LONG[0].sub, 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Gen 2 - avery", 1, INK, NULL, NULL));
    assert_nothing_outside_circle("rom long sub");
}

// ---- Pet clock ----
static ui_menu_view_t clock_view(ui_page_t page)
{
    ui_menu_view_t v = view(page);
    v.pet_s = 9 * 3600 + 58 * 60;
    v.ball_s = 14 * 3600 + 32 * 60 + 5;
    v.confirm_s = v.ball_s;
    v.clock_why = UI_CLOCK_OK;
    return v;
}

static void test_clock_row_value(void)
{
    sample_cfg();
    ui_menu_view_t v = clock_view(UI_PAGE_LIST);
    v.scroll = ui_menu_scroll_max(&v) - (38 + 4);  // one row above the bottom (Factory reset is last)
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_TRUE(find_text("Pet clock", 2, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("09:58", 1, INK, NULL, NULL));
    v.pet_s = -1;
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    TEST_ASSERT_FALSE(find_text("09:58", 1, INK, NULL, NULL));
    TEST_ASSERT_TRUE(find_text("Pet clock", 2, INK, NULL, NULL));
    // the row's value line, just under "Pet clock", is the dash: look only there (other ink can match a "-")
    int ny;
    TEST_ASSERT_TRUE(find_text("Pet clock", 2, INK, NULL, &ny));
    for (int y = 0; y < UI_H; y++)
        if (y < ny + 16 || y > ny + 30)
            for (int x = 0; x < UI_W; x++) fb[y * UI_W + x] = BG;
    TEST_ASSERT_TRUE_MESSAGE(find_text("-", 1, INK, NULL, NULL), "no dash for an unknown pet clock");
}

static void test_clock_page_texts_fit(void)
{
    sample_cfg();
    strcpy(cfg.tz, "<+0545>-5:45<+0645>,M3.5.0/02:00:00,M10.5.0/03:00:00");  // 52 chars: wider than any row
    for (int pi = 0; pi < 3; pi++) {
        uint16_t ink = ui_rgb565(PALS[pi]->ink), bg = ui_rgb565(PALS[pi]->bg);
        ui_menu_view_t v = clock_view(UI_PAGE_CLOCK);
        ui_render_menu(fb, &v, PALS[pi]);
        assert_nothing_outside_circle("clock page");
        TEST_ASSERT_TRUE(find_text("Pet clock", 2, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text("Pet 09:58:00", 2, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text("Ball 14:32:05", 2, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text(UI_ELLIPSIS, 1, soft_of(PALS[pi]), NULL, NULL));  // the long zone, cut
        int x, y;
        TEST_ASSERT_TRUE(find_text("Set to ball time", 1, bg, &x, &y));  // on the filled pill
        TEST_ASSERT_EQUAL(UI_CTL_CLOCK_SET, ui_menu_control_at(&v, x + 4, y + 4, NULL));
        v.ball_s = -1;
        v.pet_s = -1;
        ui_render_menu(fb, &v, PALS[pi]);
        TEST_ASSERT_TRUE(find_text("Pet --:--", 2, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text("Ball --:--", 2, ink, NULL, NULL));
        v.clock_busy = true;
        ui_render_menu(fb, &v, PALS[pi]);
        TEST_ASSERT_TRUE(find_text("Pet's clock menu is open", 1, soft_of(PALS[pi]), NULL, NULL));
        assert_nothing_outside_circle("clock page busy");

        v = clock_view(UI_PAGE_CLOCK_CONFIRM);
        ui_render_menu(fb, &v, PALS[pi]);
        assert_nothing_outside_circle("clock confirm");
        TEST_ASSERT_TRUE(find_text("Set pet clock", 2, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text("to 14:32?", 2, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text("Your pet may wake up", 1, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text("or fall asleep, and", 1, ink, NULL, NULL));
        TEST_ASSERT_TRUE(find_text("its age may change.", 1, ink, NULL, NULL));
        int cx, cy, sx, sy;
        TEST_ASSERT_TRUE(find_text("Cancel", 1, ink, &cx, &cy));
        TEST_ASSERT_TRUE(find_text("Set", 1, bg, &sx, &sy));
        TEST_ASSERT_EQUAL(UI_CTL_CLOCK_CANCEL, ui_menu_control_at(&v, cx + 4, cy + 4, NULL));
        TEST_ASSERT_EQUAL(UI_CTL_CLOCK_OK, ui_menu_control_at(&v, sx + 4, sy + 4, NULL));
        TEST_ASSERT_FALSE(find_text("< back", 1, soft_of(PALS[pi]), NULL, NULL));
    }
}

static void test_clock_reason_replaces_pill(void)
{
    sample_cfg();
    static const struct { ui_clock_why_t why; const char *text; } cases[] = {
        { UI_CLOCK_UNSUPPORTED, "Not available for this pet" }, { UI_CLOCK_NO_TIME, "Waiting for internet time" },
        { UI_CLOCK_CATCHUP, "Pet is catching up" }, { UI_CLOCK_PENDING, "Setting the clock..." },
    };
    ui_menu_view_t v = clock_view(UI_PAGE_CLOCK);
    ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
    int px, py;
    TEST_ASSERT_TRUE(find_text("Set to ball time", 1, BG, &px, &py));
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        v.clock_why = cases[i].why;
        ui_render_menu(fb, &v, &UI_PALETTE_CLASSIC);
        TEST_ASSERT_FALSE(find_text("Set to ball time", 1, BG, NULL, NULL));
        TEST_ASSERT_TRUE_MESSAGE(find_text(cases[i].text, 1, soft_of(&UI_PALETTE_CLASSIC), NULL, NULL), cases[i].text);
        TEST_ASSERT_EQUAL(-1, ui_menu_control_at(&v, px + 4, py + 4, NULL));
        assert_nothing_outside_circle(cases[i].text);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clock_row_value);
    RUN_TEST(test_clock_page_texts_fit);
    RUN_TEST(test_clock_reason_replaces_pill);
    RUN_TEST(test_rgb565_classic);
    RUN_TEST(test_outside_circle_black);
    RUN_TEST(test_lit_and_ghost);
    RUN_TEST(test_pixel_gap);
    RUN_TEST(test_last_pixel_position);
    RUN_TEST(test_icon_lit);
    RUN_TEST(test_p2_medicine_differs);
    RUN_TEST(test_icon_art_per_model);
    RUN_TEST(test_pet_layout);
    RUN_TEST(test_buttons_drawn);
    RUN_TEST(test_night_well_lip_darker);
    RUN_TEST(test_held_fills);
    RUN_TEST(test_no_old_hints);
    RUN_TEST(test_palette_lookup);
    RUN_TEST(test_no_ink_outside_circle);
    RUN_TEST(test_glance_unknown_pet_has_no_hearts);
    RUN_TEST(test_glance_closes_up_missing_rows);
    RUN_TEST(test_title_marquee_moves);
    RUN_TEST(test_list_long_name_ellipsis);
    RUN_TEST(test_hit_test_matches_render);
    RUN_TEST(test_controls_map_values);
    RUN_TEST(test_panel_no_wifi_message);
    RUN_TEST(test_panel_without_qr_shows_address_only);
    RUN_TEST(test_settings_list_values);
    RUN_TEST(test_regions_sorted_and_cities_split);
    RUN_TEST(test_dimming_label_line_is_no_control);
    RUN_TEST(test_long_centred_text_is_ellipsized);
    RUN_TEST(test_hit_test_matches_render_deep_scroll);
    RUN_TEST(test_pets_list_render);
    RUN_TEST(test_pet_sheet_render);
    RUN_TEST(test_pet_sheet_running_render);
    RUN_TEST(test_pet_delete_render);
    RUN_TEST(test_reset_hold_render);
    RUN_TEST(test_new_pet_list_render);
    RUN_TEST(test_new_confirm_render);
    RUN_TEST(test_rom_first_render);
    RUN_TEST(test_rom_page_empty_render);
    RUN_TEST(test_rom_long_sub_ellipsis);
    return UNITY_END();
}
