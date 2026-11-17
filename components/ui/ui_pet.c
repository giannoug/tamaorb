// SPDX-License-Identifier: GPL-3.0-or-later
// Pet screen: the 32x16 LCD at 6x, 8 icons, and the A/B/C keys under it.
#include "ui.h"
#include "ui_internal.h"

// Both icon rows sit above the LCD (the owner's Alt 4); the second row is wider to follow the round edge.
static const int ICON_C[8][2] = { { 75, 30 }, { 105, 30 }, { 135, 30 }, { 165, 30 },
                                  { 66, 57 }, { 102, 57 }, { 138, 57 }, { 174, 57 } };

static void fill(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) fb[j * UI_W + i] = c;
}

// A recessed key, no letter (like the real P1): a ghost well whose top 2 px crescent is shadow; ink when held.
static void draw_button(uint16_t *fb, int btn, bool held, uint16_t ink, uint16_t ghost, uint16_t shadow)
{
    int cx, cy;
    ui_button_centre(btn, &cx, &cy);
    const int r2 = UI_BTN_R * UI_BTN_R + UI_BTN_R;  // + r: a round edge, no 1 px nubs at the four extremes
    for (int y = cy - UI_BTN_R; y <= cy + UI_BTN_R; y++)
        for (int x = cx - UI_BTN_R; x <= cx + UI_BTN_R; x++) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r2) continue;
            const bool lip = dx * dx + (dy - 2) * (dy - 2) > r2;  // outside the disc moved 2 px down
            fb[y * UI_W + x] = held ? ink : lip ? shadow : ghost;
        }
}

void ui_render_pet(uint16_t *fb, const tc_frame_t *f, tc_model_t model, uint8_t held_mask, const ui_palette_t *p)
{
    const uint16_t bg = ui_rgb565(p->bg), ink = ui_rgb565(p->ink);
    const uint16_t ghost = ui_rgb565(ui_mix(p->bg, p->ink, p->ghost_pct));
    // the keys' top lip: 22 % black on a light palette, 40 % on a dark one (Night), where 22 % doesn't show
    const bool dark = p->bg.r + p->bg.g + p->bg.b < p->ink.r + p->ink.g + p->ink.b;
    const uint16_t shadow = ui_rgb565(ui_mix(p->bg, (ui_rgb_t){ 0, 0, 0 }, dark ? 40 : 22));

    ui_clear_rows(fb, 0, UI_H - 1, bg);

    for (int i = 0; i < 8; i++) {
        const char *const *rows = (i == 3 && model == TC_MODEL_P2) ? UI_ICON_P2_MEDICINE : UI_ICONS[i];
        const uint16_t on = (f->icons & (1u << i)) ? ink : ghost;
        const int x0 = ICON_C[i][0] - 12, y0 = ICON_C[i][1] - 12;
        for (int cy = 0; cy < 12; cy++)
            for (int cx = 0; cx < 12; cx++)
                if (rows[cy][cx] == '#') fill(fb, x0 + 2 * cx, y0 + 2 * cy, 2, 2, on);
    }

    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 32; px++)
            fill(fb, UI_LCD_X + UI_LCD_PITCH * px, UI_LCD_Y + UI_LCD_PITCH * py, UI_LCD_DOT, UI_LCD_DOT,
                 (f->rows[py] & (1u << px)) ? ink : ghost);

    for (int b = TC_BTN_A; b <= TC_BTN_C; b++)
        draw_button(fb, b, held_mask & TC_BTN_MASK(b), ink, ghost, shadow);
}

void ui_draw_lcd(uint16_t *fb, int x, int y, int scale, const tc_frame_t *f, const ui_palette_t *p)
{
    const uint16_t bg = ui_rgb565(p->bg), ink = ui_rgb565(p->ink);
    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 32; px++)
            ui_fill_rect(fb, x + scale * px, y + scale * py, scale, scale, (f->rows[py] & (1u << px)) ? ink : bg);
}
