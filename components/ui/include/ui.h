// SPDX-License-Identifier: GPL-3.0-or-later
// Screen rendering and input logic for the 240x240 round display. Portable: no ESP-IDF headers.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "settings.h"
#include "tama_core.h"
#include "tc_pet.h"
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
    bool boot_nav;          // set by the caller: a short BOOT press on a lit screen is reported by
                            // ui_screen_short_press instead of switching the screen off (glance, settings)
    bool short_pending;     // not yet collected by ui_screen_short_press()
} ui_screen_t;
// touch_buttons bit for a touch that maps to no pet button (edge band, glance, settings): it wakes the screen,
// counts as activity and is swallowed like a button, but is never part of the returned buttons.
#define UI_TOUCH_PRESENT 0x80u
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
// True once per short BOOT press made while boot_nav was set and the screen was on; that press doesn't toggle.
bool ui_screen_short_press(ui_screen_t *s);

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

// ---- Glance and settings screens (layouts from the design canvas; horizontal extents from UI_GEOM) ----
// The pet's LCD (32x16) at x, y with scale x scale px per pixel: lit pixels ink, the rest bg. No icons.
void ui_draw_lcd(uint16_t *fb, int x, int y, int scale, const tc_frame_t *f, const ui_palette_t *p);

typedef struct {
    const tc_frame_t *frame;
    const tc_pet_t *pet;        // !known: only the LCD copy, clock, battery and address are shown
    tc_model_t model;           // for the weight unit (tc_weight_unit)
    bool clock_known; uint8_t hour, minute;  // "--:--" until the clock is trusted
    int8_t batt_pct;            // -1 hides the battery icon
    const char *address;        // "tamaorb.local", an IP, or "Wi-Fi off"
} ui_glance_t;
// Rows whose data is -1 are left out and the rest close up around the screen's middle.
void ui_render_glance(uint16_t *fb, const ui_glance_t *g, const ui_palette_t *p);

typedef enum { UI_PAGE_LIST, UI_PAGE_SOUND, UI_PAGE_BRIGHTNESS, UI_PAGE_DIMMING, UI_PAGE_PALETTE,
               UI_PAGE_TZ_REGION, UI_PAGE_TZ_CITY, UI_PAGE_PANEL } ui_page_t;
// Rows of UI_PAGE_LIST, top to bottom (ui_menu_row_at's result on that page).
typedef enum { UI_ROW_SOUND, UI_ROW_BRIGHTNESS, UI_ROW_DIMMING, UI_ROW_PALETTE, UI_ROW_TZ, UI_ROW_ROM,
               UI_ROW_PANEL, UI_ROW_COUNT } ui_row_t;
// Controls reported by ui_menu_control_at, with the settings key each value is for. Values are already legal:
//   MUTE        "muted"          0/1: the value a tap switches to (the opposite of cfg->muted)
//   VOLUME      "volume"         0-100
//   BRIGHTNESS  "brightness"     1-100
//   DIM_LEVEL   "dim_level"      0-100
//   DIM_USB     "dim_usb_s"      0 (never), 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600
//   DIM_BATTERY "dim_battery_s"  same steps as DIM_USB
//   OFF_BATTERY "off_battery_s"  10, 15, 30, 60, 120, 300, 600, 1800, 3600
//   PALETTE     "palette"        st_palette_t id
// A slider reports a value for any x on its track band (clamped at the ends), so a drag can keep feeding it.
typedef enum { UI_CTL_MUTE, UI_CTL_VOLUME, UI_CTL_BRIGHTNESS, UI_CTL_DIM_LEVEL, UI_CTL_DIM_USB, UI_CTL_DIM_BATTERY,
               UI_CTL_OFF_BATTERY, UI_CTL_PALETTE } ui_ctl_t;

typedef struct {
    ui_page_t page;
    int scroll;                  // list pages: pixels scrolled (clamped to 0..ui_menu_scroll_max when drawn)
    int pressed;                 // row index highlighted while a finger is down, -1 none
    const st_settings_t *cfg;
    const char *region;          // TZ_CITY: the region being browsed, the pointer ui_menu_region_at gave (its
                                 // city count is cached per pointer)
    const char *panel_url, *panel_name;  // PANEL: URL to encode (NULL = no Wi-Fi), mDNS name
    uint32_t t_ms;               // drives marquees
} ui_menu_view_t;
void ui_render_menu(uint16_t *fb, const ui_menu_view_t *v, const ui_palette_t *p);

// Hit testing shares the layout with rendering.
int  ui_menu_row_at(const ui_menu_view_t *v, int x, int y);   // list row under a point, -1 none
bool ui_menu_back_at(const ui_menu_view_t *v, int x, int y);  // the strip above a page title (not on UI_PAGE_LIST)
// The ui_ctl_t under a point and the value it maps to, or -1 (value_out untouched).
int  ui_menu_control_at(const ui_menu_view_t *v, int x, int y, int *value_out);
int  ui_menu_row_count(const ui_menu_view_t *v);   // rows of a list page, 0 on other pages
int  ui_menu_scroll_max(const ui_menu_view_t *v);  // largest useful scroll of a list page, 0 when it all fits
// TZ_REGION row i: tzdb_regions() sorted by name ("UTC" stands for Etc/UTC); NULL past the end.
const char *ui_menu_region_at(int i);
// TZ_CITY row i of region: the i-th tzdb name starting with "<region>/" (table order); NULL past the end.
const char *ui_menu_city_at(const char *region, int i);

// Marquees: page titles wider than their strip, and the current TZ row when its name is cut, slide back and
// forth with t_ms. While ui_menu_needs_tick is true, re-render on a timer (ui_send_changed then sends only the
// rows that moved). ui_menu_tick_band says where they are: the title band, grown to cover the current row when
// that row slides too; inclusive, rows *y0..*y1.
bool ui_menu_needs_tick(const ui_menu_view_t *v);
void ui_menu_tick_band(const ui_menu_view_t *v, int *y0, int *y1);

// ---- Navigation between the pet, the glance and the settings pages ----
typedef enum { UI_SCREEN_PET, UI_SCREEN_GLANCE, UI_SCREEN_MENU } ui_screen_kind_t;
typedef struct {
    ui_screen_kind_t screen;
    ui_menu_view_t view;   // valid while screen == UI_SCREEN_MENU; view.cfg is set on every ui_nav_step
    int active_ctl;        // private: the control under the current drag's start point, -1 none
} ui_nav_t;
typedef enum { UI_ACT_NONE, UI_ACT_REDRAW, UI_ACT_SETTINGS, UI_ACT_OPEN_ROM_PICKER } ui_act_t;
void ui_nav_init(ui_nav_t *n);
// ev comes from ui_gesture_feed (MENU mode on GLANCE and MENU, PET mode on PET); cur is the latest settings and
// must stay valid until the next call (the view points at it). On UI_ACT_SETTINGS, *out is cur with exactly one
// key changed and *display_changed says whether that key is a display key. boot_short: a short BOOT press this loop.
ui_act_t ui_nav_step(ui_nav_t *n, const ui_gevent_t *ev, bool boot_short, const st_settings_t *cur,
                     st_settings_t *out, bool *display_changed);
void ui_nav_to_pet(ui_nav_t *n);   // screen off/timeout and picker close land here

// ---- Sending frames: only the rows that changed ----
// Remembers a hash of each row the panel shows. Every frame for the panel must go through ui_send_changed, or
// ui_sent_forget must be called after it, or later frames may skip rows that differ.
typedef void (*ui_send_fn)(void *ctx, const uint16_t *fb, int y0, int y1);  // rows [y0, y1)
typedef struct { uint64_t hash[UI_H]; bool valid; } ui_sent_t;
void ui_sent_init(ui_sent_t *s);                     // starts invalid: the first frame is sent whole
void ui_sent_forget(ui_sent_t *s);                   // the panel was drawn some other way: send the next frame whole
// Calls send for each run of changed rows, top to bottom (runs closer than a few rows are merged), then
// remembers fb as shown.
void ui_send_changed(ui_sent_t *s, const uint16_t *fb, ui_send_fn send, void *ctx);
