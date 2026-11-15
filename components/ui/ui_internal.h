// SPDX-License-Identifier: GPL-3.0-or-later
// Shared helpers inside the ui component.
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern const char *const UI_ICONS[8][12];      // '#' = on; ROM icon order
extern const char *const UI_ICON_P2_MEDICINE[12];

bool ui_in_circle(int x, int y);
void ui_draw_glyph(uint16_t *fb, int x, int y, char c, uint8_t scale, uint16_t color);
