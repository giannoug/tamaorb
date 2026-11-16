// SPDX-License-Identifier: GPL-3.0-or-later
// Glance screen (design canvas "Glance", layout B): clock and battery, hunger and happy hearts, a live copy of
// the pet's LCD, what the pet needs, age/weight/discipline, and the panel address. Rows without data are left
// out and the rest close up around the middle of the full layout.
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_internal.h"

// 7x7 pixel art from the canvas, drawn at scale 2.
static const char *const BOWL[7] = { ".#..#..", "..#..#.", ".......", "#######", ".#####.", "..###..", "......." };
static const char *const FACE[7] = { "..###..", ".#...#.", "#.#.#.#", "#.....#", "#.###.#", ".#...#.", "..###.." };
static const char *const HEART_FULL[7] = { ".##.##.", "#######", "#######", "#######", ".#####.", "..###..", "...#..." };
static const char *const HEART_EMPTY[7] = { ".##.##.", "#..#..#", "#.....#", "#.....#", ".#...#.", "..#.#..", "...#..." };
static const char *const BATTERY[7] = { "##########.", "#........#.", "#........##", "#........##",
                                        "#........##", "#........#.", "##########." };

#define ART 2           // scale of the 7x7 art
#define CELL (7 * ART)  // 14 px
#define HEART_GAP 4
#define GROUP_W (5 * CELL + 4 * HEART_GAP)  // icon + 4 hearts
#define GROUP_SEP 18                         // between the hunger and happy groups, shrunk to fit
#define LCD_SCALE 4
#define LCD_BORDER 2

// Rows top to bottom: height and the gap above it (canvas at device scale). The need block keeps room for
// its second line ("light is on") so the rows below don't jump when it comes and goes.
enum { R_CLOCK, R_HEARTS, R_LCD, R_NEED, R_STATS, R_ADDRESS, R_COUNT };
static const int ROW_H[R_COUNT] = { 16, CELL, 16 * LCD_SCALE + 2 * LCD_BORDER, 27, 8, 8 };
static const int GAP_ABOVE[R_COUNT] = { 0, 12, 9, 9, 6, 12 };
#define TOP_Y 17  // clock row with every row present

static void draw_hearts_group(uint16_t *fb, int x, int y, const char *const *icon, int n, uint16_t ink)
{
    ui_draw_bits(fb, x, y, icon, 7, 7, ART, ink);
    for (int i = 0; i < 4; i++)
        ui_draw_bits(fb, x + (i + 1) * (CELL + HEART_GAP), y, i < n ? HEART_FULL : HEART_EMPTY, 7, 7, ART, ink);
}

static void draw_hearts(uint16_t *fb, int y, int hunger, int happy, uint16_t ink)
{
    int x0 = 0, x1 = UI_W - 1;
    int span = ui_span(y, y + CELL - 1, &x0, &x1);
    bool both = hunger >= 0 && happy >= 0;
    int sep = GROUP_SEP;
    if (both && 2 * GROUP_W + sep > span) sep = span - 2 * GROUP_W < HEART_GAP ? HEART_GAP : span - 2 * GROUP_W;
    int total = both ? 2 * GROUP_W + sep : GROUP_W;
    int x = (x0 + x1 + 1 - total) / 2;
    if (hunger >= 0) {
        draw_hearts_group(fb, x, y, BOWL, hunger, ink);
        x += GROUP_W + sep;
    }
    if (happy >= 0) draw_hearts_group(fb, x, y, FACE, happy, ink);
}

static void draw_clock(uint16_t *fb, int y, const ui_glance_t *g, uint16_t ink)
{
    char t[8];
    if (g->clock_known) snprintf(t, sizeof t, "%02u:%02u", (unsigned)(g->hour % 24), (unsigned)(g->minute % 60));
    else strcpy(t, "--:--");
    const int bat_w = 11 * ART, gap = 8;
    int tw = ui_text_width(t, 2), total = tw + (g->batt_pct >= 0 ? gap + bat_w : 0);
    int x0 = 0, x1 = UI_W - 1;
    ui_span(y, y + 15, &x0, &x1);
    int x = (x0 + x1 + 1 - total) / 2;
    ui_draw_text(fb, x, y, t, 2, ink);
    if (g->batt_pct < 0) return;
    int bx = x + tw + gap, by = y + 1;
    ui_draw_bits(fb, bx, by, BATTERY, 11, 7, ART, ink);
    int pct = g->batt_pct > 100 ? 100 : g->batt_pct;
    int cols = (pct * 6 + 50) / 100;  // of the 6 inside columns
    ui_fill_rect(fb, bx + 2 * ART, by + 2 * ART, cols * ART, 3 * ART, ink);
}

static void draw_lcd_box(uint16_t *fb, int y, const tc_frame_t *f, const ui_palette_t *p, uint16_t ghost)
{
    int x0 = 0, x1 = UI_W - 1;
    int span = ui_span(y, y + ROW_H[R_LCD] - 1, &x0, &x1);
    int scale = LCD_SCALE;
    while (scale > 1 && 32 * scale + 2 * LCD_BORDER > span) scale--;
    int w = 32 * scale + 2 * LCD_BORDER, h = 16 * scale + 2 * LCD_BORDER;
    int x = (x0 + x1 + 1 - w) / 2, yy = y + (ROW_H[R_LCD] - h) / 2;
    ui_fill_rect(fb, x, yy, w, h, ghost);
    static const tc_frame_t blank;
    ui_draw_lcd(fb, x + LCD_BORDER, yy + LCD_BORDER, scale, f ? f : &blank, p);
}

// "3 yr  12 lb  disc 2/4", leaving out what isn't known. Empty when nothing is.
static void stats_line(const tc_pet_t *pet, tc_model_t model, char *out, size_t cap)
{
    const char *unit = tc_weight_unit(model);
    char age[12] = "", weight[16] = "", disc[12] = "";
    if (pet->age_years >= 0) snprintf(age, sizeof age, "%d yr", pet->age_years);
    if (pet->weight >= 0 && unit) snprintf(weight, sizeof weight, "%d %s", pet->weight, unit);
    if (pet->discipline >= 0) snprintf(disc, sizeof disc, "disc %d/4", pet->discipline);
    snprintf(out, cap, "%s%s%s%s%s", age, age[0] && (weight[0] || disc[0]) ? "  " : "", weight,
             weight[0] && disc[0] ? "  " : "", disc);
}

void ui_render_glance(uint16_t *fb, const ui_glance_t *g, const ui_palette_t *p)
{
    const uint16_t bg = ui_rgb565(p->bg), ink = ui_rgb565(p->ink);
    const uint16_t ghost = ui_rgb565(ui_mix(p->bg, p->ink, p->ghost_pct));
    const uint16_t soft = ui_rgb565(ui_mix(p->bg, p->ink, 75));
    ui_clear_rows(fb, 0, UI_H - 1, bg);

    static const tc_pet_t unknown = { .known = false };
    const tc_pet_t *pet = g->pet && g->pet->known ? g->pet : &unknown;
    tc_need_t need = tc_pet_need(pet);
    char stats[48] = "";
    if (pet->known) stats_line(pet, g->model, stats, sizeof stats);

    const bool show[R_COUNT] = { true, pet->known && (pet->hunger >= 0 || pet->happy >= 0), true,
                                 tc_need_word(need) != NULL, stats[0] != '\0', true };
    int full = 0, h = 0;
    for (int r = 0; r < R_COUNT; r++) {
        full += (r ? GAP_ABOVE[r] : 0) + ROW_H[r];
        if (show[r]) h += (h ? GAP_ABOVE[r] : 0) + ROW_H[r];
    }
    int y = TOP_Y + (full - h) / 2;
    for (int r = 0; r < R_COUNT; r++) {
        if (!show[r]) continue;
        if (r != R_CLOCK) y += GAP_ABOVE[r];
        switch (r) {
        case R_CLOCK: draw_clock(fb, y, g, ink); break;
        case R_HEARTS: draw_hearts(fb, y, pet->hunger, pet->happy, ink); break;
        case R_LCD: draw_lcd_box(fb, y, g->frame, p, ghost); break;
        case R_NEED:
            ui_text_centred_at(fb, y, tc_need_word(need), 2, ink);
            if (need == TC_NEED_ASLEEP_LIGHT_ON) ui_text_centred_at(fb, y + 19, "light is on", 1, ink);
            break;
        case R_STATS: ui_text_centred_at(fb, y, stats, 1, ink); break;
        case R_ADDRESS: ui_text_centred_at(fb, y, g->address ? g->address : "", 1, soft); break;
        }
        y += ROW_H[r];
    }
}
