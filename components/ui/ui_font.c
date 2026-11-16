// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.h"
#include "ui_internal.h"
#include "font8x8_basic.h"

void ui_draw_glyph(uint16_t *fb, int x, int y, char c, uint8_t scale, uint16_t color)
{
    ui_draw_glyph_clip(fb, x, y, c, scale, color, 0, UI_W);
}

void ui_draw_glyph_clip(uint16_t *fb, int x, int y, char c, uint8_t scale, uint16_t color, int cx0, int cx1)
{
    const unsigned char *g = font8x8_basic[(unsigned char)c & 0x7F];
    if (cx0 < 0) cx0 = 0;
    if (cx1 > UI_W) cx1 = UI_W;
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++) {
            if (!(g[row] & (1u << col))) continue;
            int px0 = x + col * scale, px1 = px0 + scale;  // [px0, px1) clipped to [cx0, cx1)
            if (px0 < cx0) px0 = cx0;
            if (px1 > cx1) px1 = cx1;
            if (px0 < px1) ui_fill_rect(fb, px0, y + row * scale, px1 - px0, scale, color);
        }
}
