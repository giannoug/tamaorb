// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.h"
#include "ui_internal.h"

const ui_palette_t UI_PALETTE_CLASSIC = { .bg = { 0xB6, 0xC1, 0x9E }, .ink = { 0x1C, 0x25, 0x18 }, .ghost_pct = 11 };

uint16_t ui_rgb565(ui_rgb_t c)
{
    return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

static uint8_t mix1(uint8_t a, uint8_t b, uint8_t pct)
{
    int v = a * 100 + (b - a) * pct;
    return (uint8_t)((v + (v >= 0 ? 50 : -50)) / 100);
}

ui_rgb_t ui_mix(ui_rgb_t a, ui_rgb_t b, uint8_t pct)
{
    return (ui_rgb_t){ mix1(a.r, b.r, pct), mix1(a.g, b.g, pct), mix1(a.b, b.b, pct) };
}

bool ui_in_circle(int x, int y)
{
    // Pixel centres at (x + 0.5, y + 0.5) against a radius-120 circle centred at (120,120); doubled to stay integer.
    int dx = 2 * x + 1 - 240, dy = 2 * y + 1 - 240;
    return dx * dx + dy * dy <= 240 * 240;
}
