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

// ---- Touch ----
typedef struct { uint8_t count; struct { int16_t x, y; } pt[2]; } ui_touch_t;
// OR of the zone masks of the active points: x < 80 → A, 80..159 → B, >= 160 → C.
uint8_t ui_touch_buttons(const ui_touch_t *t);

// Stateful mapping used on the device. The CST816D averages two fingers at the same height into
// one point (measured), so A+C is also a slide: a single finger keeps the button it started on
// while it moves, and a finger that started on A and reaches C (or C and reaches A) holds A+C.
// Genuine two-point reports still map through ui_touch_buttons.
typedef struct { bool down; uint8_t start_mask; } ui_touch_tracker_t;
uint8_t ui_touch_track(ui_touch_tracker_t *tr, const ui_touch_t *t);

// ---- Screen timeout and BOOT button ----
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

// ---- Text screens (layouts from the design canvas, ASCII only) ----
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

// ---- Geometry and text helpers (layouts take positions from these, never from round-screen literals) ----
#define UI_ELLIPSIS "\x7f"  // single glyph: three dots on the baseline row
typedef struct { int16_t w, h; bool round; } ui_geom_t;
extern const ui_geom_t UI_GEOM;  // {UI_W, UI_H, true} on this board
// Widest x range [*x0, *x1] visible on every row y0..y1 (clamped to the screen); returns its width, 0 if none.
int  ui_geom_span(const ui_geom_t *g, int y0, int y1, int *x0, int *x1);
// Largest centred square that is fully visible (round: inscribed square; otherwise the whole screen).
void ui_geom_safe(const ui_geom_t *g, int *x, int *y, int *w, int *h);

// ---- Gesture classifier (pet taps, edge swipes, menu taps and drags) ----
// Edge bands are the top, bottom, left and right strips of the screen, all UI_EDGE_BAND thick (10 % of the
// smaller side). Top/bottom can complete a swipe; left/right only make the touch inert (no pet button).
#define UI_EDGE_BAND_FOR(g)  (((g)->w < (g)->h ? (g)->w : (g)->h) / 10)   // 24 px on 240 px; to be calibrated on the ball
#define UI_SWIPE_DIST_FOR(g) ((g)->h / 4)    // 60 px on 240 px
#define UI_EDGE_BAND   UI_EDGE_BAND_FOR(&UI_GEOM)
#define UI_SWIPE_DIST  UI_SWIPE_DIST_FOR(&UI_GEOM)
#define UI_TAP_SLOP    8

typedef enum { UI_G_NONE, UI_G_SWIPE_DOWN, UI_G_SWIPE_UP, UI_G_TAP, UI_G_DRAG } ui_gkind_t;
// x, y: current point (tap: where it landed). Swipes: dx, dy are the total movement since the touch began.
// Drags: dx, dy since the previous sample; released is true on the final event of a moved touch (deltas 0).
// sx, sy: where pt[0] of this touch went down (taps, drags and swipes). first: this is the touch's first event
// other than UI_G_NONE, so a consumer can drop per-touch state from a touch whose end it never saw.
typedef struct { ui_gkind_t kind; int16_t x, y; int16_t dx, dy; bool released; int16_t sx, sy; bool first; } ui_gevent_t;
typedef enum { UI_GMODE_PET, UI_GMODE_MENU } ui_gmode_t;
typedef struct {
    // Private state: callers only allocate it (zero-init is valid), reset it, and feed it.
    ui_touch_tracker_t tr;
    const ui_geom_t *geom;   // NULL means UI_GEOM
    uint8_t phase, side;     // side: 1 top band, 2 bottom band, from pt[0] at touch start
    ui_gmode_t mode;
    bool fresh;              // just reset: a touch already down is ignored until it lifts
    bool moved;
    bool emitted;            // this touch already reported an event
    int16_t sx, sy, lx, ly;
} ui_gesture_t;

// One call per touch sample (every UI loop). PET mode: a touch that starts outside the edge bands returns
// the buttons ui_touch_track would, on every sample. A touch that starts in a band returns 0 buttons for
// its whole life (also when it slides out of the band or a second finger joins) and may complete one swipe
// (inward by UI_SWIPE_DIST with |dx| < |dy|); only pt[0] starting in a top/bottom band arms a swipe.
// A touch whose pt[0] starts in the left or right band (and not in a top/bottom band) is inert for its whole
// life: no buttons, no event, whether it is a tap, a hold or a sideways swipe. A corner counts as top/bottom.
// MENU mode has no buttons: a tap is reported on release if the finger moved less than UI_TAP_SLOP, otherwise
// drags report per-sample deltas. A MENU touch whose pt[0] starts in a band behaves the same and, on the sample
// that passes the swipe test, reports one SWIPE_DOWN/UP instead of a DRAG; the rest of that touch is inert
// (no further drag, no tap or released drag on lift).
// A mode change during a touch makes the rest of that touch inert.
uint8_t ui_gesture_feed(ui_gesture_t *g, ui_gmode_t mode, const ui_touch_t *t, uint32_t now_ms, ui_gevent_t *ev);
// After mode changes or overlays; a touch that is down at the next feed is ignored until released.
void ui_gesture_reset(ui_gesture_t *g);
// Starts from a clean state with a non-default geometry (tests, other boards). The pointer must outlive the gesture.
void ui_gesture_set_geom(ui_gesture_t *g, const ui_geom_t *geom);

int  ui_text_fit(const char *s, uint8_t scale, int max_w);  // chars of the first line that fit whole in max_w
// Draws s clipped to [x0,x1); if it doesn't fit, draws the longest prefix that fits with room for UI_ELLIPSIS,
// then the ellipsis. Returns the drawn width.
int  ui_draw_text_ellipsis(uint16_t *fb, int x0, int x1, int y, const char *s, uint8_t scale, uint16_t color);
// Ping-pong scroll offset (0..overflow) for text wider than its box: 1000 ms pause, 30 px/s, 1000 ms pause, back.
int  ui_marquee_offset(int text_w, int box_w, uint32_t t_ms);
// Draws s starting at x0 - dx, writing only inside [x0,x1). Returns the text width.
int  ui_draw_text_clipped(uint16_t *fb, int x0, int x1, int y, int dx, const char *s, uint8_t scale, uint16_t color);

// Draws text (byte mode, ECC medium, smallest version, up to version 4) as a QR code centred at (cx, cy),
// with a 2-module light quiet zone, at the largest integer module size that fits max_px.
// Returns the drawn side in px; 0 (nothing drawn) if the text is too long or max_px is too small. No heap.
int  ui_draw_qr(uint16_t *fb, int cx, int cy, int max_px, const char *text, uint16_t dark, uint16_t light);
