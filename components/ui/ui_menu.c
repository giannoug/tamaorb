// SPDX-License-Identifier: GPL-3.0-or-later
// Settings screens (design canvas "Settings", "Sound", "Brightness", "Dimming", "Palette", "ZoneRegion",
// "ZoneCity", "Panel") and their hit testing. Vertical positions are the canvas's at device scale; every
// horizontal extent comes from the screen geometry (ui_span / ui_geom_safe).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tzdb.h"
#include "ui.h"
#include "ui_internal.h"

// ---- Layout ----
#define BACK_Y 15         // "< back", scale 1
#define TITLE_Y 29        // page title, scale 2
#define TITLE_MARGIN 5    // title strip: the visible span of its rows, less this on each side
#define LIST_TITLE_Y 22   // "Settings"
#define ROW_PAD_X 7
#define ROW_RADIUS 5
#define ROW_GAP 4
#define LIST_PAD 4        // above the first row and below the last
#define LIST_INSET 2      // rows sit this far inside the visible span of the list area

typedef struct { int top, row_h; } list_layout_t;
static const list_layout_t LIST_LAYOUT[] = {
    [UI_PAGE_LIST] = { 50, 38 }, [UI_PAGE_TZ_REGION] = { 66, 28 }, [UI_PAGE_TZ_CITY] = { 52, 28 },
};
#define REGION_NOW_Y 51   // "now: Isle of Man" under the Time zone title

static bool is_list(ui_page_t p) { return p == UI_PAGE_LIST || p == UI_PAGE_TZ_REGION || p == UI_PAGE_TZ_CITY; }
static int list_bottom(void) { return UI_GEOM.h - UI_GEOM.h / 6 - 1; }  // last row of the list area

// The list area and the x range of its rows: inside the visible span of every row of the area.
static void list_box(ui_page_t page, int *top, int *bottom, int *x0, int *x1)
{
    *top = LIST_LAYOUT[page].top;
    *bottom = list_bottom();
    *x0 = 0;
    *x1 = UI_W - 1;
    ui_span(*top, *bottom, x0, x1);
    *x0 += LIST_INSET;
    *x1 -= LIST_INSET;
}

// Controls live in the safe square (the canvas's 36..204 on this board).
static void control_x(int *x0, int *x1)
{
    int sx, sy, sw, sh;
    ui_geom_safe(&UI_GEOM, &sx, &sy, &sw, &sh);
    *x0 = sx;
    *x1 = sx + sw - 1;
}

// ---- Data: regions, cities, labels ----
#define MAX_REGIONS 32
static const char *s_regions[MAX_REGIONS];
static int s_region_n = -1;

static int cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

const char *ui_menu_region_at(int i)
{
    if (s_region_n < 0) {
        s_region_n = tzdb_regions(s_regions, MAX_REGIONS);
        qsort(s_regions, (size_t)s_region_n, sizeof s_regions[0], cmp_str);
    }
    return i >= 0 && i < s_region_n ? s_regions[i] : NULL;
}

static bool in_region(const char *name, const char *region, size_t rn)
{
    return strncmp(name, region, rn) == 0 && name[rn] == '/';
}

const char *ui_menu_city_at(const char *region, int i)
{
    if (!region || i < 0) return NULL;
    size_t rn = strlen(region);
    for (int k = 0, n = tzdb_count(); k < n; k++)
        if (in_region(tzdb_at(k)->name, region, rn) && i-- == 0) return tzdb_at(k)->name;
    return NULL;
}

// Cached per region pointer (regions come from ui_menu_region_at, so a pointer always names the same region):
// list layout asks for the count on every render and hit test.
static int city_count(const char *region)
{
    static const char *s_region;
    static int s_count;
    if (!region) return 0;
    if (region == s_region) return s_count;
    size_t rn = strlen(region);
    int c = 0;
    for (int k = 0, n = tzdb_count(); k < n; k++) c += in_region(tzdb_at(k)->name, region, rn);
    s_region = region;
    s_count = c;
    return c;
}

// The part after the first '/', with '_' shown as ' '.
static void city_label(const char *name, char *out, size_t cap)
{
    const char *s = strchr(name, '/');
    s = s ? s + 1 : name;
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) out[i] = s[i] == '_' ? ' ' : s[i];
    out[i] = '\0';
}

// How cfg->tz is shown: the city of a table zone, "UTC" for Etc/UTC, "Custom" for a POSIX string.
static void tz_label(const char *tz, char *out, size_t cap)
{
    if (!strcmp(tz, "Etc/UTC")) snprintf(out, cap, "UTC");
    else if (tzdb_find(tz)) city_label(tz, out, cap);
    else snprintf(out, cap, "Custom");
}

// The region row cfg->tz belongs to ("UTC" for Etc/UTC), or "" for none.
static void tz_region(const char *tz, char *out, size_t cap)
{
    const char *slash = strchr(tz, '/');
    out[0] = '\0';
    if (!strcmp(tz, "Etc/UTC")) snprintf(out, cap, "UTC");
    else if (tzdb_find(tz) && slash) snprintf(out, cap, "%.*s", (int)(slash - tz), tz);
}

static void duration(unsigned s, char *out, size_t cap)
{
    if (s >= 3600 && s % 3600 == 0) snprintf(out, cap, "%u h", s / 3600);
    else if (s >= 60 && s % 60 == 0) snprintf(out, cap, "%u min", s / 60);
    else snprintf(out, cap, "%u s", s);
}

static const char *const PALETTE_NAMES[3] = { "Classic", "Neutral", "Night" };

static void list_value(const st_settings_t *c, int row, char *out, size_t cap)
{
    char d[16];
    out[0] = '\0';
    switch (row) {
    case UI_ROW_SOUND:
        if (c->muted) snprintf(out, cap, "muted");
        else snprintf(out, cap, "%u %%", c->volume);
        break;
    case UI_ROW_BRIGHTNESS: snprintf(out, cap, "%u %%", c->brightness); break;
    case UI_ROW_DIMMING:
        duration(c->dim_battery_s, d, sizeof d);
        if (c->dim_battery_s) snprintf(out, cap, "after %s", d);
        else snprintf(out, cap, "never");
        break;
    case UI_ROW_PALETTE: snprintf(out, cap, "%s", PALETTE_NAMES[c->palette <= ST_PALETTE_NIGHT ? c->palette : 0]); break;
    case UI_ROW_TZ: tz_label(c->tz, out, cap); break;
    case UI_ROW_PANEL: snprintf(out, cap, "QR code"); break;
    default: break;  // ROM: the picker shows the choices
    }
}

static const char *const LIST_NAMES[UI_ROW_COUNT] = { "Sound", "Brightness", "Dimming", "Palette", "Time zone",
                                                      "ROM", "Web panel" };

// ---- Rows of the list pages ----
int ui_menu_row_count(const ui_menu_view_t *v)
{
    switch (v->page) {
    case UI_PAGE_LIST: return UI_ROW_COUNT;
    case UI_PAGE_TZ_REGION: return ui_menu_region_at(0) ? s_region_n : 0;
    case UI_PAGE_TZ_CITY: return city_count(v->region);
    default: return 0;
    }
}

int ui_menu_scroll_max(const ui_menu_view_t *v)
{
    if (!is_list(v->page)) return 0;
    int n = ui_menu_row_count(v), top, bottom, x0, x1;
    list_box(v->page, &top, &bottom, &x0, &x1);
    int content = 2 * LIST_PAD + n * LIST_LAYOUT[v->page].row_h + (n > 0 ? (n - 1) * ROW_GAP : 0);
    int view = bottom - top + 1;
    return content > view ? content - view : 0;
}

static int clamp_scroll(const ui_menu_view_t *v)
{
    int m = ui_menu_scroll_max(v);
    return v->scroll < 0 ? 0 : v->scroll > m ? m : v->scroll;
}

// Top of row i; scroll is clamp_scroll(v), worked out once per render or hit test by the caller.
static int row_top(const ui_menu_view_t *v, int i, int scroll)
{
    return LIST_LAYOUT[v->page].top + LIST_PAD + i * (LIST_LAYOUT[v->page].row_h + ROW_GAP) - scroll;
}

int ui_menu_row_at(const ui_menu_view_t *v, int x, int y)
{
    if (!is_list(v->page)) return -1;
    int top, bottom, x0, x1;
    list_box(v->page, &top, &bottom, &x0, &x1);
    if (y < top || y > bottom || x < x0 || x > x1) return -1;
    int step = LIST_LAYOUT[v->page].row_h + ROW_GAP;
    int rel = y - row_top(v, 0, clamp_scroll(v));
    if (rel < 0) return -1;
    int i = rel / step;
    if (rel % step >= LIST_LAYOUT[v->page].row_h || i >= ui_menu_row_count(v)) return -1;
    return i;
}

bool ui_menu_back_at(const ui_menu_view_t *v, int x, int y)
{
    return v->page != UI_PAGE_LIST && y >= 0 && y < TITLE_Y && x >= 0 && x < UI_W;
}

// The row that is the current choice on the TZ pages, -1 if none.
static int current_row(const ui_menu_view_t *v)
{
    char r[32];
    if (v->page == UI_PAGE_TZ_REGION) {
        tz_region(v->cfg->tz, r, sizeof r);
        for (int i = 0; ui_menu_region_at(i); i++)
            if (!strcmp(ui_menu_region_at(i), r)) return i;
    } else if (v->page == UI_PAGE_TZ_CITY && v->region) {
        size_t rn = strlen(v->region);
        for (int k = 0, i = 0, n = tzdb_count(); k < n; k++) {
            const char *name = tzdb_at(k)->name;
            if (!in_region(name, v->region, rn)) continue;
            if (!strcmp(name, v->cfg->tz)) return i;
            i++;
        }
    }
    return -1;
}

// ---- Titles and marquees ----
static const char *page_title(const ui_menu_view_t *v)
{
    switch (v->page) {
    case UI_PAGE_LIST: return "Settings";
    case UI_PAGE_SOUND: return "Sound";
    case UI_PAGE_BRIGHTNESS: return "Brightness";
    case UI_PAGE_DIMMING: return "Dimming";
    case UI_PAGE_PALETTE: return "Palette";
    case UI_PAGE_TZ_REGION: return "Time zone";
    case UI_PAGE_TZ_CITY: return v->region ? v->region : "";
    case UI_PAGE_PANEL: return "Web panel";
    }
    return "";
}

static int title_y(const ui_menu_view_t *v) { return v->page == UI_PAGE_LIST ? LIST_TITLE_Y : TITLE_Y; }

static int title_box(int y, int *x0, int *x1)
{
    *x0 = 0;
    *x1 = UI_W - 1;
    ui_span(y, y + 15, x0, x1);
    *x0 += TITLE_MARGIN;
    *x1 -= TITLE_MARGIN;
    return *x1 - *x0 + 1;
}

static bool title_moves(const ui_menu_view_t *v)
{
    int x0, x1;
    return ui_text_width(page_title(v), 2) > title_box(title_y(v), &x0, &x1);
}

// The name strip of row i (scale 2 text) on a TZ page.
static void tz_name_box(int x0, int x1, bool current, ui_page_t page, int *n0, int *n1)
{
    *n0 = x0 + ROW_PAD_X;
    *n1 = x1 - ROW_PAD_X;  // inclusive
    if (page == UI_PAGE_TZ_REGION) *n1 -= 7 + 4;        // room for '>'
    else if (current) *n1 -= 7 * 2 + 4;                 // room for the check mark
}

static void row_name(const ui_menu_view_t *v, int i, char *out, size_t cap)
{
    if (v->page == UI_PAGE_TZ_REGION) snprintf(out, cap, "%s", ui_menu_region_at(i));
    else city_label(ui_menu_city_at(v->region, i), out, cap);
}

// True if the current TZ row is (partly) in the list area and its name is too long for its strip.
static bool current_row_moves(const ui_menu_view_t *v, int *y0, int *y1)
{
    int cur = current_row(v);
    if (cur < 0) return false;
    int top, bottom, x0, x1, n0, n1;
    list_box(v->page, &top, &bottom, &x0, &x1);
    int ry = row_top(v, cur, clamp_scroll(v)), rh = LIST_LAYOUT[v->page].row_h;
    if (ry + rh <= top || ry > bottom) return false;
    char name[64];
    row_name(v, cur, name, sizeof name);
    tz_name_box(x0, x1, true, v->page, &n0, &n1);
    if (ui_text_width(name, 2) <= n1 - n0 + 1) return false;
    *y0 = ry < top ? top : ry;
    *y1 = ry + rh - 1 > bottom ? bottom : ry + rh - 1;
    return true;
}

bool ui_menu_needs_tick(const ui_menu_view_t *v)
{
    int a, b;
    return title_moves(v) || current_row_moves(v, &a, &b);
}

void ui_menu_tick_band(const ui_menu_view_t *v, int *y0, int *y1)
{
    *y0 = title_y(v);
    *y1 = *y0 + 15;
    int a, b;
    if (current_row_moves(v, &a, &b)) {
        if (a < *y0) *y0 = a;
        if (b > *y1) *y1 = b;
    }
}

// ---- Drawing ----
typedef struct { uint16_t bg, ink, ghost, soft, thumb; } colors_t;

static colors_t colors_of(const ui_palette_t *p)
{
    ui_rgb_t ghost = ui_mix(p->bg, p->ink, p->ghost_pct);
    return (colors_t){ ui_rgb565(p->bg), ui_rgb565(p->ink), ui_rgb565(ghost), ui_rgb565(ui_mix(p->bg, p->ink, 75)),
                       ui_rgb565(ui_mix(ghost, p->ink, 60)) };  // scrollbar thumb: ink at 60 % over the track
}

static void draw_title(uint16_t *fb, const ui_menu_view_t *v, uint16_t ink)
{
    const char *s = page_title(v);
    int y = title_y(v), x0, x1;
    int box = title_box(y, &x0, &x1), tw = ui_text_width(s, 2);
    if (tw <= box) ui_draw_text(fb, x0 + (box - tw) / 2, y, s, 2, ink);
    else ui_draw_text_clipped(fb, x0, x1 + 1, y, ui_marquee_offset(tw, box, v->t_ms), s, 2, ink);
}

static void draw_header(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    if (v->page != UI_PAGE_LIST) ui_text_centred_at(fb, BACK_Y, "< back", 1, c->soft);
    draw_title(fb, v, c->ink);
}

static void draw_scrollbar(uint16_t *fb, const colors_t *c, int top, int bottom, int scroll, int max)
{
    if (max == 0) return;
    const int track_h = 90, w = 3;
    int view = bottom - top + 1;
    int ty = top + (view - track_h) / 2, x0, x1 = UI_W - 1;
    ui_span(ty, ty + track_h - 1, &x0, &x1);
    int tx = x1 - 8;
    int thumb = track_h * view / (view + max);
    if (thumb < 16) thumb = 16;
    int pos = (track_h - thumb) * scroll / max;
    ui_fill_rect(fb, tx, ty, w, track_h, c->ghost);
    ui_fill_rect(fb, tx, ty + pos, w, thumb, c->thumb);
}

static const char *const CHECK[6] = { "......#", ".....#.", "#...#..", ".#.#...", "..#....", "......." };

static void draw_list(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    int top, bottom, x0, x1;
    list_box(v->page, &top, &bottom, &x0, &x1);
    const int rh = LIST_LAYOUT[v->page].row_h, n = ui_menu_row_count(v), cur = current_row(v);
    const int max = ui_menu_scroll_max(v), scroll = v->scroll < 0 ? 0 : v->scroll > max ? max : v->scroll;
    // Rows wholly above the list area are skipped without a look: row i ends at top + LIST_PAD + i*step + rh - scroll.
    const int step = rh + ROW_GAP, i0 = scroll > LIST_PAD + rh ? (scroll - LIST_PAD - rh) / step : 0;
    char name[64], value[64];
    for (int i = i0; i < n; i++) {
        int y = row_top(v, i, scroll);
        if (y + rh <= top) continue;
        if (y > bottom) break;
        bool hi = i == v->pressed || i == cur;
        uint16_t fill = hi ? c->ink : c->ghost, fg = hi ? c->bg : c->ink;
        ui_fill_rrect(fb, x0, y, x1 - x0 + 1, rh, ROW_RADIUS, fill);
        int tx0 = x0 + ROW_PAD_X, tx1 = x1 - ROW_PAD_X + 1;  // text strip [tx0, tx1)
        if (v->page == UI_PAGE_LIST) {
            ui_draw_text_ellipsis(fb, tx0, tx1, y + 5, LIST_NAMES[i], 2, fg);
            list_value(v->cfg, i, value, sizeof value);
            ui_draw_text_ellipsis(fb, tx0, tx1 - 7 - 4, y + 25, value, 1, fg);
            ui_draw_text(fb, tx1 - 7, y + 25, ">", 1, fg);
            continue;
        }
        int n0, n1;
        tz_name_box(x0, x1, i == cur, v->page, &n0, &n1);
        row_name(v, i, name, sizeof name);
        int tw = ui_text_width(name, 2), box = n1 - n0 + 1;
        if (i == cur && tw > box) ui_draw_text_clipped(fb, n0, n1 + 1, y + 6, ui_marquee_offset(tw, box, v->t_ms), name, 2, fg);
        else ui_draw_text_ellipsis(fb, n0, n1 + 1, y + 6, name, 2, fg);
        if (v->page == UI_PAGE_TZ_REGION) ui_draw_text(fb, tx1 - 7, y + 10, ">", 1, fg);
        else if (i == cur) ui_draw_bits(fb, tx1 - 14, y + 8, CHECK, 7, 6, 2, fg);
    }
    // Rows reach past the list area while scrolling: repaint above and below it, then the header goes on top.
    ui_clear_rows(fb, 0, top - 1, c->bg);
    ui_clear_rows(fb, bottom + 1, UI_H - 1, c->bg);
    draw_scrollbar(fb, c, top, bottom, scroll, max);
}

// Slider: track of height th at y (rounded), filled to frac (num/den), thumb of diameter d centred on it.
static void draw_slider(uint16_t *fb, int x0, int x1, int y, int th, int d, int num, int den,
                        uint16_t track, uint16_t fill, uint16_t bg, uint16_t border)
{
    int w = x1 - x0 + 1, at = den ? w * num / den : 0;
    ui_fill_rrect(fb, x0, y, w, th, th / 2, track);
    if (at > 0) ui_fill_rrect(fb, x0, y, at, th, th / 2, fill);
    int tx = x0 + at - d / 2, ty = y + th / 2 - d / 2;
    ui_fill_disc(fb, tx, ty, d, border);
    ui_fill_disc(fb, tx + 2, ty + 2, d - 4, bg);
}

// ---- Sound ----
#define MUTE_Y 62
#define MUTE_H 36
#define SWITCH_W 52
#define SWITCH_H 26
#define VOLUME_Y 118
#define VOLUME_TRACK_Y 156
#define SOUND_HINT_Y 186
#define TRACK_H 8
#define THUMB_D 24

static void draw_sound(uint16_t *fb, const ui_menu_view_t *v, const ui_palette_t *p, const colors_t *c)
{
    int x0, x1;
    control_x(&x0, &x1);
    const st_settings_t *cfg = v->cfg;
    ui_draw_text(fb, x0, MUTE_Y + (MUTE_H - 16) / 2, "Mute", 2, c->ink);
    int sx = x1 - SWITCH_W + 1, sy = MUTE_Y + (MUTE_H - SWITCH_H) / 2;
    ui_fill_rrect(fb, sx, sy, SWITCH_W, SWITCH_H, SWITCH_H / 2, c->ink);
    if (!cfg->muted) ui_fill_rrect(fb, sx + 2, sy + 2, SWITCH_W - 4, SWITCH_H - 4, SWITCH_H / 2 - 2, c->bg);
    ui_fill_disc(fb, cfg->muted ? sx + SWITCH_W - 22 : sx + 4, sy + 4, 18, cfg->muted ? c->bg : c->ink);

    // Muted: the volume is shown faded (45 %) since it isn't what the speaker plays.
    uint8_t a = cfg->muted ? 45 : 100;
    uint16_t ink = ui_rgb565(ui_mix(p->bg, p->ink, a));
    uint16_t ghost = ui_rgb565(ui_mix(p->bg, p->ink, (uint8_t)(p->ghost_pct * a / 100)));
    char val[16];
    snprintf(val, sizeof val, "%u %%", cfg->volume);
    ui_draw_text(fb, x0, VOLUME_Y, "Volume", 2, ink);
    ui_draw_text(fb, x1 + 1 - ui_text_width(val, 1), VOLUME_Y + 8, val, 1, ink);
    draw_slider(fb, x0, x1, VOLUME_TRACK_Y, TRACK_H, THUMB_D, cfg->volume, 100, ghost, ink, c->bg, ink);
    char hint[40];
    if (cfg->muted) snprintf(hint, sizeof hint, "unmute restores %u %%", cfg->volume);
    else snprintf(hint, sizeof hint, "speaker is loud");
    ui_text_centred_at(fb, SOUND_HINT_Y, hint, 1, c->soft);
}

// ---- Brightness ----
#define SUN_Y 70
#define BRIGHT_VALUE_Y 128
#define BRIGHT_TRACK_Y 164
#define BRIGHT_HINT_Y 194
static const char *const SUN[12] = { ".....##.....", ".##..##..##.", ".##......##.", "....####....",
                                     "...######...", "##.######.##", "##.######.##", "...######...",
                                     "....####....", ".##......##.", ".##..##..##.", ".....##....." };

static void draw_brightness(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    int x0, x1, s0, s1;
    control_x(&x0, &x1);
    ui_span(SUN_Y, SUN_Y + 47, &s0, &s1);
    ui_draw_bits(fb, (s0 + s1 + 1 - 48) / 2, SUN_Y, SUN, 12, 12, 4, c->ink);
    char val[16];
    snprintf(val, sizeof val, "%u %%", v->cfg->brightness);
    ui_text_centred_at(fb, BRIGHT_VALUE_Y, val, 2, c->ink);
    draw_slider(fb, x0, x1, BRIGHT_TRACK_Y, TRACK_H, THUMB_D, v->cfg->brightness, 100, c->ghost, c->ink, c->bg, c->ink);
    ui_text_centred_at(fb, BRIGHT_HINT_Y, "changes as you drag", 1, c->soft);
}

// ---- Dimming ----
#define DIM_Y 60
#define DIM_STEP 33
#define DIM_TRACK_DY 16
#define DIM_TRACK_H 6
#define DIM_THUMB_D 20
#define DIM_INSET 4
static const uint16_t DIM_STEPS[] = { 0, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600 };
static const uint16_t OFF_STEPS[] = { 10, 15, 30, 60, 120, 300, 600, 1800, 3600 };
#define N_DIM (int)(sizeof DIM_STEPS / sizeof DIM_STEPS[0])
#define N_OFF (int)(sizeof OFF_STEPS / sizeof OFF_STEPS[0])

typedef struct { const char *label; int ctl; } dim_row_t;
static const dim_row_t DIM_ROWS[4] = {
    { "Dim level", UI_CTL_DIM_LEVEL }, { "Dim on USB", UI_CTL_DIM_USB },
    { "Dim on batt", UI_CTL_DIM_BATTERY }, { "Off on batt", UI_CTL_OFF_BATTERY },
};

static int nearest_step(const uint16_t *steps, int n, unsigned v)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if (abs((int)steps[i] - (int)v) < abs((int)steps[best] - (int)v)) best = i;
    return best;
}

static void dim_x(int *x0, int *x1)
{
    control_x(x0, x1);
    *x0 += DIM_INSET;
    *x1 -= DIM_INSET;
}

static void draw_dimming(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    int x0, x1;
    dim_x(&x0, &x1);
    const st_settings_t *cfg = v->cfg;
    for (int r = 0; r < 4; r++) {
        int y = DIM_Y + r * DIM_STEP, num, den;
        char val[16];
        unsigned s = r == 1 ? cfg->dim_usb_s : r == 2 ? cfg->dim_battery_s : cfg->off_battery_s;
        if (r == 0) {
            snprintf(val, sizeof val, "%u %%", cfg->dim_level);
            num = cfg->dim_level;
            den = 100;
        } else if (r < 3) {
            if (s) duration(s, val, sizeof val);
            else snprintf(val, sizeof val, "never");
            num = nearest_step(DIM_STEPS, N_DIM, s);
            den = N_DIM - 1;
        } else {
            duration(s, val, sizeof val);
            num = nearest_step(OFF_STEPS, N_OFF, s);
            den = N_OFF - 1;
        }
        ui_draw_text(fb, x0, y, DIM_ROWS[r].label, 1, c->ink);
        ui_draw_text(fb, x1 + 1 - ui_text_width(val, 1), y, val, 1, c->ink);
        draw_slider(fb, x0, x1, y + DIM_TRACK_DY, DIM_TRACK_H, DIM_THUMB_D, num, den, c->ghost, c->ink, c->bg, c->ink);
    }
}

// ---- Palette ----
#define TILE_Y 74
#define TILE 56
#define TILE_GAP 8
#define TILE_RADIUS 8
#define PALETTE_NAME_Y 150
#define PALETTE_HINT_Y 180
static const char *const PET[11] = { "....####....", "..##....##..", ".#........#.", "#..#....#..#",
                                     "#..........#", "#...####...#", "#..........#", ".#........#.",
                                     "..########..", "..#......#..", "............" };

static int tile_x(int i)
{
    int x0, x1;
    ui_span(TILE_Y, TILE_Y + TILE - 1, &x0, &x1);
    return (x0 + x1 + 1 - (3 * TILE + 2 * TILE_GAP)) / 2 + i * (TILE + TILE_GAP);
}

static void draw_palette(uint16_t *fb, const ui_menu_view_t *v, const ui_palette_t *p, const colors_t *c)
{
    int cur = v->cfg->palette <= ST_PALETTE_NIGHT ? (int)v->cfg->palette : 0;
    uint16_t edge = ui_rgb565(ui_mix(p->bg, p->ink, 30));
    for (int i = 0; i < 3; i++) {
        const ui_palette_t *tp = ui_palette_by_id(i);
        int x = tile_x(i), b = i == cur ? 3 : 1;
        ui_fill_rrect(fb, x, TILE_Y, TILE, TILE, TILE_RADIUS, i == cur ? c->ink : edge);
        ui_fill_rrect(fb, x + b, TILE_Y + b, TILE - 2 * b, TILE - 2 * b, TILE_RADIUS - b, ui_rgb565(tp->bg));
        ui_draw_bits(fb, x + (TILE - 36) / 2, TILE_Y + (TILE - 33) / 2, PET, 12, 11, 3, ui_rgb565(tp->ink));
    }
    ui_text_centred_at(fb, PALETTE_NAME_Y, PALETTE_NAMES[cur], 2, c->ink);
    ui_text_centred_at(fb, PALETTE_HINT_Y, "tap one to use it", 1, c->soft);
}

// ---- Web panel ----
#define QR_Y 55
#define QR_PLATE 104
#define PANEL_ADDR_Y 169
#define PANEL_NAME_Y 183
#define NO_WIFI_Y 100
static const ui_rgb_t QR_LIGHT = { 0xF4, 0xF4, 0xEC }, QR_DARK = { 0x11, 0x11, 0x11 };  // fixed for scanners

static void draw_panel(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    if (!v->panel_url) {
        ui_text_centred_at(fb, NO_WIFI_Y, "Wi-Fi not connected", 1, c->ink);
        ui_text_centred_at(fb, NO_WIFI_Y + 16, "set it up over USB", 1, c->soft);
        return;
    }
    int x0, x1;
    ui_span(QR_Y, QR_Y + QR_PLATE - 1, &x0, &x1);
    int px = (x0 + x1 + 1 - QR_PLATE) / 2;
    uint16_t light = ui_rgb565(QR_LIGHT);
    ui_fill_rrect(fb, px, QR_Y, QR_PLATE, QR_PLATE, 3, light);
    ui_draw_qr(fb, px + QR_PLATE / 2, QR_Y + QR_PLATE / 2, QR_PLATE, v->panel_url, ui_rgb565(QR_DARK), light);
    // The address without "http://" and the trailing '/'.
    const char *u = strncmp(v->panel_url, "http://", 7) ? v->panel_url : v->panel_url + 7;
    char host[64];
    snprintf(host, sizeof host, "%.*s", (int)strcspn(u, "/"), u);
    ui_text_centred_at(fb, PANEL_ADDR_Y, host, 1, c->ink);
    if (v->panel_name) ui_text_centred_at(fb, PANEL_NAME_Y, v->panel_name, 1, c->soft);
}

void ui_render_menu(uint16_t *fb, const ui_menu_view_t *v, const ui_palette_t *p)
{
    colors_t c = colors_of(p);
    ui_clear_rows(fb, 0, UI_H - 1, c.bg);
    switch (v->page) {
    case UI_PAGE_LIST:
    case UI_PAGE_TZ_CITY: draw_list(fb, v, &c); break;
    case UI_PAGE_TZ_REGION: {
        draw_list(fb, v, &c);
        char now[80], label[64];
        tz_label(v->cfg->tz, label, sizeof label);
        snprintf(now, sizeof now, "now: %s", label);
        int x0, x1;
        ui_span(REGION_NOW_Y, REGION_NOW_Y + 7, &x0, &x1);
        int tw = ui_text_width(now, 1), box = x1 - x0 + 1 - 2 * TITLE_MARGIN;
        if (tw <= box) ui_text_centred_at(fb, REGION_NOW_Y, now, 1, c.soft);
        else ui_draw_text_ellipsis(fb, x0 + TITLE_MARGIN, x1 + 1 - TITLE_MARGIN, REGION_NOW_Y, now, 1, c.soft);
        break;
    }
    case UI_PAGE_SOUND: draw_sound(fb, v, p, &c); break;
    case UI_PAGE_BRIGHTNESS: draw_brightness(fb, v, &c); break;
    case UI_PAGE_DIMMING: draw_dimming(fb, v, &c); break;
    case UI_PAGE_PALETTE: draw_palette(fb, v, p, &c); break;
    case UI_PAGE_PANEL: draw_panel(fb, v, &c); break;
    }
    draw_header(fb, v, &c);
}

// ---- Controls ----
static int frac_of(int x, int x0, int x1, int steps)  // 0..steps, nearest, clamped
{
    int w = x1 - x0 + 1;
    int k = ((x - x0) * steps * 2 + w) / (2 * w);
    return k < 0 ? 0 : k > steps ? steps : k;
}

int ui_menu_control_at(const ui_menu_view_t *v, int x, int y, int *value_out)
{
    int x0, x1, ctl = -1, val = 0;
    control_x(&x0, &x1);
    switch (v->page) {
    case UI_PAGE_SOUND:
        if (y >= MUTE_Y && y < MUTE_Y + MUTE_H) { ctl = UI_CTL_MUTE; val = !v->cfg->muted; }
        else if (y >= VOLUME_TRACK_Y - THUMB_D / 2 && y < VOLUME_TRACK_Y + TRACK_H + THUMB_D / 2) {
            ctl = UI_CTL_VOLUME;
            val = frac_of(x, x0, x1, 100);
        }
        break;
    case UI_PAGE_BRIGHTNESS:
        if (y >= BRIGHT_TRACK_Y - THUMB_D / 2 && y < BRIGHT_TRACK_Y + TRACK_H + THUMB_D / 2) {
            ctl = UI_CTL_BRIGHTNESS;
            val = frac_of(x, x0, x1, 100);
            if (val < 1) val = 1;
        }
        break;
    case UI_PAGE_DIMMING:
        dim_x(&x0, &x1);
        for (int r = 0; r < 4; r++) {
            // the track and its thumb (rows ry+9..ry+28), not the label line above them
            int ty = DIM_Y + r * DIM_STEP + DIM_TRACK_DY + DIM_TRACK_H / 2 - DIM_THUMB_D / 2;
            if (y < ty || y >= ty + DIM_THUMB_D) continue;
            ctl = DIM_ROWS[r].ctl;
            if (r == 0) val = frac_of(x, x0, x1, 100);
            else if (r < 3) val = DIM_STEPS[frac_of(x, x0, x1, N_DIM - 1)];
            else val = OFF_STEPS[frac_of(x, x0, x1, N_OFF - 1)];
        }
        break;
    case UI_PAGE_PALETTE:
        for (int i = 0; i < 3; i++)
            if (x >= tile_x(i) && x < tile_x(i) + TILE && y >= TILE_Y && y < TILE_Y + TILE) { ctl = UI_CTL_PALETTE; val = i; }
        break;
    default: break;
    }
    if (ctl >= 0 && value_out) *value_out = val;
    return ctl;
}
