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
                   uint8_t held_mask, const ui_palette_t *p);

// ---- Touch ----
// The pet's LCD on the pet screen: 32x16 dots at pitch UI_LCD_PITCH (UI_LCD_DOT lit), 192x96 at (UI_LCD_X, UI_LCD_Y).
#define UI_LCD_X     24
#define UI_LCD_Y     72   // centred: 72 + 96/2 = 120
#define UI_LCD_PITCH 6
#define UI_LCD_DOT   5
#define UI_BTN_R     10   // pet button radius: 20 px across, no letter (like the real P1)

// Pet-screen buttons. The strip is y from the LCD's bottom + 2 to the bottom edge band (170..215 on 240x240).
// The keys sit on an arc concentric with the screen: B at (UI_W/2, strip bottom - UI_BTN_R), A and C 56 px to
// either side on the same arc ((64,184), (120,205), (176,184) on 240x240).
void ui_button_centre(int btn, int *x, int *y);   // btn = TC_BTN_A/B/C
// TC_BTN_A/B/C for a point in the strip (split halfway between the centres, so every circle presses only its
// own button), -1 elsewhere (the LCD, the icons above it, the bottom edge band).
int  ui_button_at(int x, int y);

typedef struct { uint8_t count; struct { int16_t x, y; } pt[2]; } ui_touch_t;
// OR of the buttons under the active points (ui_button_at); points off the strip add nothing.
uint8_t ui_touch_buttons(const ui_touch_t *t);

// Stateful mapping used on the device. A touch whose first point is off the strip (the LCD, the top icons)
// presses nothing until it lifts, even if it slides into the strip. One that starts in the strip keeps the
// button it started on while it moves, anywhere on the screen. The CST816D averages two fingers at the same
// height into one point (measured), so A+C is also a slide: a finger that started on A and reaches C's part of
// the strip's width (or C and reaches A's) holds A+C; once started only x counts. Genuine two-point reports
// still map through ui_touch_buttons.
typedef struct { bool down; uint8_t start_mask; } ui_touch_tracker_t;   // start_mask 0 while down: inert touch
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
// Catching up (canvas CatchupSkip, CatchupStop): name may be NULL ("Your pet"). skip: the Skip pill, or its confirm.
typedef enum { UI_SKIP_BUTTON, UI_SKIP_CONFIRM } ui_skip_t;
typedef enum { UI_CU_NONE, UI_CU_SKIP, UI_CU_GO_ON, UI_CU_STOP } ui_cu_hit_t;
void ui_render_catchup(uint16_t *fb, const char *name, uint64_t done_s, uint64_t total_s, uint32_t eta_s,
                       ui_skip_t skip, const ui_palette_t *p);
ui_cu_hit_t ui_catchup_at(ui_skip_t skip, int x, int y);
// The resume question (canvas Resume, ResumeBack): back_name NULL means no Back pill (not after a switch).
typedef struct { const char *name; uint64_t gap_s; uint32_t ff_s; const char *back_name; } ui_resume_view_t;
typedef enum { UI_RESUME_NONE, UI_RESUME_RESUME, UI_RESUME_FF, UI_RESUME_BACK } ui_resume_hit_t;
void ui_render_resume(uint16_t *fb, const ui_resume_view_t *v, const ui_palette_t *p);
ui_resume_hit_t ui_resume_at(const ui_resume_view_t *v, int x, int y);
// "40s", "12m", "5h 12m", "3d 4h": the two largest units, the second left out when 0; largest_only: "3d".
void ui_fmt_away(uint64_t s, bool largest_only, char *out, size_t cap);
void ui_render_low_battery(uint16_t *fb, const ui_palette_t *p);

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

// An encoded QR code (the device encodes with espressif/qrcode; ui only draws).
#define UI_QR_MAX 33  // modules per side at QR version 4
typedef struct { uint8_t size; uint8_t mod[UI_QR_MAX][UI_QR_MAX]; } ui_qr_t;  // mod[y][x]: 1 dark; size 0: none
// Draws qr centred at (cx, cy) with a 2-module light quiet zone at the largest integer module size that fits
// max_px; clips to the screen. Returns the side in px; 0 (nothing drawn) if qr is NULL, size is 0 or above
// UI_QR_MAX, or max_px is too small.
int  ui_draw_qr(uint16_t *fb, int cx, int cy, int max_px, const ui_qr_t *qr, uint16_t dark, uint16_t light);

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

// UI_PAGE_ROM: the New pet list (a ROM per row), and the first-start picker (rom_first).
// UI_PAGE_ROM_CONFIRM: "New egg?". UI_PAGE_PETS: the pets, then "+ New pet"; PET_SHEET: one pet; PET_DELETE: confirm.
typedef enum { UI_PAGE_LIST, UI_PAGE_SOUND, UI_PAGE_BRIGHTNESS, UI_PAGE_DIMMING, UI_PAGE_PALETTE,
               UI_PAGE_TZ_REGION, UI_PAGE_TZ_CITY, UI_PAGE_PANEL, UI_PAGE_ROM, UI_PAGE_ROM_CONFIRM,
               UI_PAGE_CLOCK, UI_PAGE_CLOCK_CONFIRM, UI_PAGE_PETS, UI_PAGE_PET_SHEET, UI_PAGE_PET_DELETE,
               UI_PAGE_RESET } ui_page_t;
// Rows of UI_PAGE_LIST, top to bottom (ui_menu_row_at's result on that page).
typedef enum { UI_ROW_SOUND, UI_ROW_BRIGHTNESS, UI_ROW_DIMMING, UI_ROW_PALETTE, UI_ROW_TZ, UI_ROW_CLOCK, UI_ROW_PETS,
               UI_ROW_PANEL, UI_ROW_RESET, UI_ROW_COUNT } ui_row_t;
// Controls reported by ui_menu_control_at, with the settings key each value is for. Values are already legal:
//   MUTE        "muted"          0/1: the value a tap switches to (the opposite of cfg->muted)
//   VOLUME      "volume"         0-100
//   BRIGHTNESS  "brightness"     1-100
//   DIM_LEVEL   "dim_level"      0-100
//   DIM_USB     "dim_usb_s"      0 (never), 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600
//   DIM_BATTERY "dim_battery_s"  same steps as DIM_USB
//   OFF_BATTERY "off_battery_s"  10, 15, 30, 60, 120, 300, 600, 1800, 3600
//   PALETTE     "palette"        st_palette_t id
//   NEW_CANCEL, NEW_START        the New pet confirm's pills (no setting; value 0)
//   PET_CANCEL, PET_SWITCH       the pet sheet's pills (PET_CANCEL is Close on the running pet's sheet)
//   PET_DELETE                   the pet sheet's small "Delete..." pill
//   DEL_KEEP, DEL_DELETE         the delete confirm's pills
//   RESET_HOLD                   the factory reset's "Hold to erase" pill (held, never tapped: ui_nav_hold)
//   CLOCK_SET                    the Pet clock page's pill, only while clock_why is UI_CLOCK_OK (value 0)
//   CLOCK_CANCEL, CLOCK_OK       the Pet clock confirm's pills (value 0)
// A slider reports a value for any x on its track band (clamped at the ends), so a drag can keep feeding it.
typedef enum { UI_CTL_MUTE, UI_CTL_VOLUME, UI_CTL_BRIGHTNESS, UI_CTL_DIM_LEVEL, UI_CTL_DIM_USB, UI_CTL_DIM_BATTERY,
               UI_CTL_OFF_BATTERY, UI_CTL_PALETTE, UI_CTL_NEW_CANCEL, UI_CTL_NEW_START, UI_CTL_CLOCK_SET,
               UI_CTL_CLOCK_CANCEL, UI_CTL_CLOCK_OK, UI_CTL_PET_CANCEL, UI_CTL_PET_SWITCH, UI_CTL_PET_DELETE,
               UI_CTL_DEL_KEEP, UI_CTL_DEL_DELETE, UI_CTL_RESET_HOLD } ui_ctl_t;

// Why the Pet clock page offers no Set pill (UI_CLOCK_OK: it does). PENDING: a request is out.
typedef enum { UI_CLOCK_OK, UI_CLOCK_UNSUPPORTED, UI_CLOCK_NO_TIME, UI_CLOCK_CATCHUP, UI_CLOCK_PENDING } ui_clock_why_t;

// One row of the ROM picker: the model name ("P1"), a line under it ("Gen 1 - tama.bin"), the file for the confirm,
// and whether it can be picked (false: greyed, "not on the SD card").
typedef struct { const char *name, *sub, *file; bool enabled; } ui_rom_row_t;
// One pet: its name, the list's line ("Gen 2 - egg - 3d"), the sheet's two lines ("Gen 2 - egg", "away 3d 4h" or
// "running now"), and whether it can be switched to (false: its ROM is not on the SD card).
typedef struct { const char *name, *sub, *model_age, *away; bool can_switch; } ui_pet_row_t;

typedef struct {
    ui_page_t page;
    int scroll;                  // list pages: pixels scrolled (clamped to 0..ui_menu_scroll_max when drawn)
    int pressed;                 // row index highlighted while a finger is down, -1 none
    const st_settings_t *cfg;
    const char *region;          // TZ_CITY: the region being browsed, the pointer ui_menu_region_at gave (its
                                 // city count is cached per pointer)
    const char *panel_url, *panel_name;  // PANEL: the panel's URL (NULL = no Wi-Fi), mDNS name
    const ui_qr_t *panel_qr;     // PANEL: panel_url encoded; NULL or size 0: the address only
    uint32_t t_ms;               // now: drives marquees and the confirm guard; set it before ui_nav_step and rendering
    // ROM and ROM_CONFIRM. The caller fills roms/rom_count while roms is NULL (ui_nav_open_roms
    // leaves it NULL; ui_nav_to_pet clears it); the rows must stay valid while the page shows.
    const ui_rom_row_t *roms;    // NULL: not loaded yet (nothing listed)
    int rom_count;
    int rom_pick;                // the row tapped: first start's pick, or the New pet confirm's ROM
    bool rom_first;              // first start: "Pick a pet", no back, no confirm: a tap picks
    const char *new_name;        // ROM_CONFIRM: the new pet's name ("Angel #2"), filled by the caller
    const char *running_name;    // ROM_CONFIRM: the running pet's name
    // PETS, PET_SHEET, PET_DELETE. The caller fills pets/pet_count/pet_current while pets is NULL (ui_nav_open_pets
    // leaves it NULL); the rows must stay valid while those pages show.
    const ui_pet_row_t *pets;
    int pet_count;
    int pet_current;             // row of the running pet, -1 if not listed
    int pet_pick;                // the pet the sheet and the delete confirm are about
    int hold_pct;                // RESET: how far the 3 s hold has got (0-100); ui_nav_hold sets it
    // The settings list's Pet clock row and the CLOCK pages. The caller fills pet_s, ball_s, clock_why and
    // clock_busy every loop while they show. Seconds of the day; -1 unknown (row "-", page "--:--").
    int32_t pet_s, ball_s;
    ui_clock_why_t clock_why;
    bool clock_busy;             // the last attempt timed out: "Pet's clock menu is open" above the pill
    int32_t confirm_s;           // set by ui_nav when the confirm opens: the ball time it asks about
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
    bool pets_from_pet;    // "< back" on the Pets list returns to the pet (else to the settings list)
    uint32_t confirm_ms;   // private: view.t_ms when the confirm page opened
    int rom_scroll;        // private: the ROM list's scroll when its confirm opened (restored on the way back)
    int pets_scroll;       // private: the Pets list's scroll when a pet's sheet opened
    uint32_t hold_t0;      // private: when the current hold on the reset pill began
    bool holding, hold_fired;  // private: a hold is under way; it already fired (until the finger lifts)
} ui_nav_t;
// The confirm page's pills ignore taps for this long after it opens (by view.t_ms), so the second half of a
// double tap on a list row that lies where a pill appears can't switch pets.
#define UI_CONFIRM_GUARD_MS 500
// ROM_SWITCH: first start picked view.roms[view.rom_pick].
// CLOCK_SYNC: the Pet clock confirm's Set was tapped; nav is back on UI_PAGE_CLOCK. Post the request.
// PET_SWITCH, PET_DELETE: about view.pets[view.pet_pick] (confirmed). PET_NEW: a new pet of view.roms[view.rom_pick].
// FACTORY_RESET: the reset pill was held for UI_RESET_HOLD_MS.
typedef enum { UI_ACT_NONE, UI_ACT_REDRAW, UI_ACT_SETTINGS, UI_ACT_ROM_SWITCH, UI_ACT_CLOCK_SYNC, UI_ACT_PET_SWITCH,
               UI_ACT_PET_DELETE, UI_ACT_PET_NEW, UI_ACT_FACTORY_RESET } ui_act_t;
#define UI_RESET_HOLD_MS 3000
void ui_nav_init(ui_nav_t *n);
// ev comes from ui_gesture_feed (MENU mode on GLANCE and MENU, PET mode on PET); cur is the latest settings and
// must stay valid until the next call (the view points at it). On UI_ACT_SETTINGS, *out is cur with exactly one
// key changed and *display_changed says whether that key is a display key. boot_short: a short BOOT press this loop.
ui_act_t ui_nav_step(ui_nav_t *n, const ui_gevent_t *ev, bool boot_short, const st_settings_t *cur,
                     st_settings_t *out, bool *display_changed);
void ui_nav_to_pet(ui_nav_t *n);   // screen off/timeout and picker close land here
// Every loop on the factory reset page: down and (x, y) are the finger now. A finger that stays on the reset pill
// for UI_RESET_HOLD_MS returns FACTORY_RESET once (until it lifts); lifting or sliding off empties the bar.
// REDRAW when view.hold_pct changed; NONE elsewhere.
ui_act_t ui_nav_hold(ui_nav_t *n, bool down, int x, int y, uint32_t now_ms);
// The ROM list (MENU, page ROM, roms NULL, rom_pick -1, rom_first false): the New pet list, whose back
// returns to the Pets list; first start sets rom_first. The caller then fills the rows.
void ui_nav_open_roms(ui_nav_t *n);
// The Pets list (MENU, page PETS, pets NULL, pet_current/pet_pick -1). from_pet: a BOOT long press opened it, so
// back returns to the pet. The caller then fills the rows.
void ui_nav_open_pets(ui_nav_t *n, bool from_pet);

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
