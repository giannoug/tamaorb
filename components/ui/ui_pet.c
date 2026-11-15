// SPDX-License-Identifier: GPL-3.0-or-later
// Pet screen: the 32x16 LCD at 6x plus 8 icons (spec §5.1, Global Constraints geometry).
#include "ui.h"
#include "ui_internal.h"

#define LCD_X 24
#define LCD_Y 72
#define LCD_PITCH 6
#define LCD_DOT 5

static const int ICON_CX[4] = { 57, 97, 143, 183 };
static const int ICON_CY[2] = { 48, 192 };

static int zone_of(int x) { return x < 80 ? TC_BTN_A : (x < 160 ? TC_BTN_B : TC_BTN_C); }

static void fill(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) fb[j * UI_W + i] = c;
}

static void draw_hints(uint16_t *fb, uint16_t c)
{
    static const int lines_x[2] = { 79, 159 };
    static const int spans[2][2] = { { 11, 31 }, { 206, 226 } };
    for (int l = 0; l < 2; l++)
        for (int s = 0; s < 2; s++)
            for (int y = spans[s][0]; y <= spans[s][1]; y++)
                if (((y - spans[s][0]) & 3) < 2) fb[y * UI_W + lines_x[l]] = c;  // 2 px on, 2 px off
    ui_draw_glyph(fb, 66, 210, 'A', 1, c);
    ui_draw_glyph(fb, 116, 214, 'B', 1, c);
    ui_draw_glyph(fb, 166, 210, 'C', 1, c);
}

void ui_render_pet(uint16_t *fb, const tc_frame_t *f, tc_model_t model,
                   uint8_t held_mask, bool hints, const ui_palette_t *p)
{
    const uint16_t bg = ui_rgb565(p->bg), ink = ui_rgb565(p->ink);
    const uint16_t ghost = ui_rgb565(ui_mix(p->bg, p->ink, p->ghost_pct));
    const uint16_t tint = ui_rgb565(ui_mix(p->bg, p->ink, 10));

    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++)
            fb[y * UI_W + x] = !ui_in_circle(x, y) ? 0x0000
                             : (held_mask & TC_BTN_MASK(zone_of(x))) ? tint : bg;

    if (hints) draw_hints(fb, ui_rgb565(ui_mix(p->bg, p->ink, 42)));

    for (int i = 0; i < 8; i++) {
        const char *const *rows = (i == 3 && model == TC_MODEL_P2) ? UI_ICON_P2_MEDICINE : UI_ICONS[i];
        const uint16_t on = (f->icons & (1u << i)) ? ink : ghost;
        const int x0 = ICON_CX[i % 4] - 12, y0 = ICON_CY[i / 4] - 12;
        for (int cy = 0; cy < 12; cy++)
            for (int cx = 0; cx < 12; cx++)
                if (rows[cy][cx] == '#') fill(fb, x0 + 2 * cx, y0 + 2 * cy, 2, 2, on);
    }

    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 32; px++)
            fill(fb, LCD_X + LCD_PITCH * px, LCD_Y + LCD_PITCH * py, LCD_DOT, LCD_DOT,
                 (f->rows[py] & (1u << px)) ? ink : ghost);
}
