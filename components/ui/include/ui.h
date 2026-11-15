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
extern const ui_palette_t UI_PALETTE_NEUTRAL, UI_PALETTE_NIGHT;  // #C4C6BD/#22241F, #18201A/#A9D38E
// 0 classic, 1 neutral, 2 night (the st_palette_t values); anything else gives Classic.
const ui_palette_t *ui_palette_by_id(int st_palette_id);

uint16_t ui_rgb565(ui_rgb_t c);
ui_rgb_t ui_mix(ui_rgb_t a, ui_rgb_t b, uint8_t pct);  // a + (b - a) * pct / 100, rounded to nearest

// Framebuffer: UI_W * UI_H RGB565 pixels in host byte order (the board swaps for the panel).
void ui_render_pet(uint16_t *fb, const tc_frame_t *f, tc_model_t model,
                   uint8_t held_mask, bool hints, const ui_palette_t *p);

// ---- Touch (spec §5.2) ----
typedef struct { uint8_t count; struct { int16_t x, y; } pt[2]; } ui_touch_t;
// OR of the zone masks of the active points: x < 80 → A, 80..159 → B, >= 160 → C.
uint8_t ui_touch_buttons(const ui_touch_t *t);

// Stateful mapping used on the device. The CST816D averages two fingers at the same height into
// one point (measured), so A+C is also a slide: a single finger keeps the button it started on
// while it moves, and a finger that started on A and reaches C (or C and reaches A) holds A+C.
// Genuine two-point reports still map through ui_touch_buttons.
typedef struct { bool down; uint8_t start_mask; } ui_touch_tracker_t;
uint8_t ui_touch_track(ui_touch_tracker_t *tr, const ui_touch_t *t);

// ---- Screen timeout and BOOT button (spec §5.3–5.4) ----
typedef struct {
    bool on;
    uint32_t last_activity_ms;
    bool swallowing;        // the touch that woke the screen is not forwarded until released
    bool boot_was_down;
    uint32_t boot_down_ms;
    bool sound_was_on;
    bool long_fired;        // this BOOT hold already reported a long press
    bool long_pending;      // not yet collected by ui_screen_long_press()
} ui_screen_t;
typedef struct { uint8_t buttons; bool backlight; } ui_screen_out_t;

void ui_screen_init(ui_screen_t *s, uint32_t now_ms);  // starts on
// sound_quiet: while true a sound neither wakes the screen nor counts as activity.
// timeout_ms == 0: no automatic timeout (e.g. on USB power); BOOT can still switch the screen off.
ui_screen_out_t ui_screen_update(ui_screen_t *s, uint32_t now_ms, uint8_t touch_buttons,
                                 bool boot_down, bool sound_on, bool sound_quiet, uint32_t timeout_ms);
void ui_screen_wake(ui_screen_t *s, uint32_t now_ms);  // e.g. USB plugged in
// True while the screen is on but has been idle (no touch, sound or BOOT) for at least dim_ms.
bool ui_screen_dimmed(const ui_screen_t *s, uint32_t now_ms, uint32_t dim_ms);
// Restarts the state machine after an overlay (e.g. the picker) closed: a BOOT press or touch that
// is still held then is ignored until released, so it neither toggles the screen nor reaches the game.
void ui_screen_resume(ui_screen_t *s, uint32_t now_ms, bool boot_down, bool touch_down);
// True once per BOOT hold of >= 2 s (reported while still held); that press doesn't toggle the screen.
bool ui_screen_long_press(ui_screen_t *s);

// ---- Text screens (spec §5.5; layouts from the design canvas, ASCII only) ----
// Draws with the 8x8 font; '\n' starts a new line (11 px * scale below). Returns the text width.
int  ui_draw_text(uint16_t *fb, int x, int y, const char *s, uint8_t scale, uint16_t color);
int  ui_text_width(const char *line, uint8_t scale);  // first line only; the trailing blank column is excluded
void ui_render_message(uint16_t *fb, const char *title, const char *body, const ui_palette_t *p);
void ui_render_catchup(uint16_t *fb, uint64_t done_s, uint64_t total_s, uint32_t eta_s, const ui_palette_t *p);
void ui_render_low_battery(uint16_t *fb, const ui_palette_t *p);
void ui_render_picker(uint16_t *fb, const char *const *names, const char *const *subs,
                      int n, int selected, const ui_palette_t *p);

// Picker geometry, shared with the touch handling that selects entries.
#define UI_PICKER_X      40
#define UI_PICKER_W      160
#define UI_PICKER_TOP    70
#define UI_PICKER_H      32
#define UI_PICKER_STEP   37
#define UI_PICKER_MAX    3
