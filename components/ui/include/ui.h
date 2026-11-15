// SPDX-License-Identifier: GPL-3.0-or-later
// Screen rendering and input logic for the 240x240 round display. Portable: no ESP-IDF headers.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "tama_core.h"
#include "tc_rom.h"

#define UI_W 240
#define UI_H 240

typedef struct { uint8_t r, g, b; } ui_rgb_t;
typedef struct { ui_rgb_t bg, ink; uint8_t ghost_pct; } ui_palette_t;
extern const ui_palette_t UI_PALETTE_CLASSIC;  // bg #B6C19E, ink #1C2518, ghost 11 %

uint16_t ui_rgb565(ui_rgb_t c);
ui_rgb_t ui_mix(ui_rgb_t a, ui_rgb_t b, uint8_t pct);  // a + (b - a) * pct / 100, rounded to nearest

// Framebuffer: UI_W * UI_H RGB565 pixels in host byte order (the board swaps for the panel).
void ui_render_pet(uint16_t *fb, const tc_frame_t *f, tc_model_t model,
                   uint8_t held_mask, bool hints, const ui_palette_t *p);

// ---- Touch (spec §5.2) ----
typedef struct { uint8_t count; struct { int16_t x, y; } pt[2]; } ui_touch_t;
// OR of the zone masks of the active points: x < 80 → A, 80..159 → B, >= 160 → C.
uint8_t ui_touch_buttons(const ui_touch_t *t);

// ---- Screen timeout and BOOT button (spec §5.3–5.4) ----
typedef struct {
    bool on;
    uint32_t last_activity_ms;
    bool swallowing;        // the touch that woke the screen is not forwarded until released
    bool boot_was_down;
    uint32_t boot_down_ms;
    bool sound_was_on;
} ui_screen_t;
typedef struct { uint8_t buttons; bool backlight; } ui_screen_out_t;

void ui_screen_init(ui_screen_t *s, uint32_t now_ms);  // starts on
ui_screen_out_t ui_screen_update(ui_screen_t *s, uint32_t now_ms, uint8_t touch_buttons,
                                 bool boot_down, bool sound_on, uint32_t timeout_ms);
