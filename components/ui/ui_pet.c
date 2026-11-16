// SPDX-License-Identifier: GPL-3.0-or-later
// Pet screen: the 32x16 LCD at 6x, 8 icons, and the A/B/C keys under it.
#include "ui.h"
#include "ui_internal.h"

static const int ICON_CX[4] = { 57, 97, 143, 183 };
static const int ICON_CY[2] = { 38, 164 };  // top row partly in the top swipe band: drawn, never pressed

static void fill(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) fb[j * UI_W + i] = c;
}

// A raised key: shadow 2 px lower, ink outline 2 px, face bg (ink when held), letter at scale 2 in the other colour.
static void draw_button(uint16_t *fb, int btn, bool held, uint16_t bg, uint16_t ink, uint16_t shadow)
{
    int cx, cy;
    ui_button_centre(btn, &cx, &cy);
    const int d = 2 * UI_BTN_R, x = cx - UI_BTN_R, y = cy - UI_BTN_R;
    ui_fill_disc(fb, x, y + 2, d, shadow);
    ui_fill_disc(fb, x, y, d, ink);
    ui_fill_disc(fb, x + 2, y + 2, d - 4, held ? ink : bg);
    const char letter[2] = { (char)('A' + btn - TC_BTN_A), 0 };
    ui_draw_text(fb, cx - ui_text_width(letter, 2) / 2, cy - 7, letter, 2, held ? bg : ink);
}

void ui_render_pet(uint16_t *fb, const tc_frame_t *f, tc_model_t model, uint8_t held_mask, const ui_palette_t *p)
{
    const uint16_t bg = ui_rgb565(p->bg), ink = ui_rgb565(p->ink);
    const uint16_t ghost = ui_rgb565(ui_mix(p->bg, p->ink, p->ghost_pct));
    const uint16_t shadow = ui_rgb565(ui_mix(p->bg, (ui_rgb_t){ 0, 0, 0 }, 22));

    ui_clear_rows(fb, 0, UI_H - 1, bg);

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
            fill(fb, UI_LCD_X + UI_LCD_PITCH * px, UI_LCD_Y + UI_LCD_PITCH * py, UI_LCD_DOT, UI_LCD_DOT,
                 (f->rows[py] & (1u << px)) ? ink : ghost);

    for (int b = TC_BTN_A; b <= TC_BTN_C; b++)
        draw_button(fb, b, held_mask & TC_BTN_MASK(b), bg, ink, shadow);
}

void ui_draw_lcd(uint16_t *fb, int x, int y, int scale, const tc_frame_t *f, const ui_palette_t *p)
{
    const uint16_t bg = ui_rgb565(p->bg), ink = ui_rgb565(p->ink);
    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 32; px++)
            ui_fill_rect(fb, x + scale * px, y + scale * py, scale, scale, (f->rows[py] & (1u << px)) ? ink : bg);
}
