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
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++) {
            if (!(g[row] & (1u << col))) continue;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++) {
                    int px = x + col * scale + dx, py = y + row * scale + dy;
                    if (px >= 0 && px < UI_W && px >= cx0 && px < cx1 && py >= 0 && py < UI_H) fb[py * UI_W + px] = color;
                }
        }
}
