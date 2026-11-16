// SPDX-License-Identifier: GPL-3.0-or-later
// Shared helpers inside the ui component.
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern const char *const UI_ICONS[8][12];      // '#' = on; ROM icon order
extern const char *const UI_ICON_P2_MEDICINE[12];

bool ui_in_circle(int x, int y);
void ui_draw_glyph(uint16_t *fb, int x, int y, char c, uint8_t scale, uint16_t color);
// Same, but only pixels with cx0 <= x < cx1 are written.
void ui_draw_glyph_clip(uint16_t *fb, int x, int y, char c, uint8_t scale, uint16_t color, int cx0, int cx1);

// Drawing primitives for the screens. They clip to the framebuffer only: layouts keep inside the visible area.
void ui_clear_rows(uint16_t *fb, int y0, int y1, uint16_t bg);  // bg where UI_GEOM is visible, 0 elsewhere
void ui_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void ui_fill_rrect(uint16_t *fb, int x, int y, int w, int h, int r, uint16_t c);  // corners of radius r
void ui_fill_disc(uint16_t *fb, int x, int y, int d, uint16_t c);                 // circle in the d x d box at x, y
// Pixel art: rows of w chars, '#' = on, each drawn as a scale x scale block.
void ui_draw_bits(uint16_t *fb, int x, int y, const char *const *rows, int w, int h, int scale, uint16_t c);
// ui_geom_span of UI_GEOM with the rows clamped to the screen first.
int  ui_span(int y0, int y1, int *x0, int *x1);
// Draws one line of text centred in the visible span of its rows; returns its left x. Text wider than the span
// is drawn from the span's left edge, cut with UI_ELLIPSIS (ui_draw_text_ellipsis).
int  ui_text_centred_at(uint16_t *fb, int y, const char *s, uint8_t scale, uint16_t color);
