// SPDX-License-Identifier: GPL-3.0-or-later
// Settings screens (design canvas "Settings", "Sound", "Brightness", "Dimming", "Palette", "ZoneRegion",
// "ZoneCity", "Panel", "PickerFirst", and the "Pets" row: PetsList, PetSheet, PetSheetRunning, PetDelete, NewPet,
// NewPetConfirm) and their hit testing. Vertical positions are the canvas's at device scale; every
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
    [UI_PAGE_ROM] = { 50, 34 }, [UI_PAGE_PETS] = { 50, 34 },
};
#define REGION_NOW_Y 51   // "now: Isle of Man" under the Time zone title

// ROM picker (canvas PickerList / PickerFirst / PickerConfirm)
#define ROM_FIRST_TITLE_Y 44  // "Pick a pet"
#define ROM_FIRST_TOP 66      // its list starts lower, under the title
#define ROM_HINT_Y 176        // "tap a pet to switch"; the list ends above it
#define ROM_HINT_GAP 15       // first start: the hint follows the rows this far below, up to ROM_HINT_Y
#define ROM_EMPTY_Y 100       // "No ROMs on the SD card"

static bool is_list(ui_page_t p)
{
    return p == UI_PAGE_LIST || p == UI_PAGE_TZ_REGION || p == UI_PAGE_TZ_CITY || p == UI_PAGE_ROM ||
           p == UI_PAGE_PETS;
}
static bool is_tz(ui_page_t p) { return p == UI_PAGE_TZ_REGION || p == UI_PAGE_TZ_CITY; }
static int list_top(const ui_menu_view_t *v)
{
    return v->page == UI_PAGE_ROM && v->rom_first ? ROM_FIRST_TOP : LIST_LAYOUT[v->page].top;
}
// Last row of the list area: the first-start list stops above its hint line.
static int list_bottom(const ui_menu_view_t *v)
{
    return v->page == UI_PAGE_ROM && v->rom_first ? ROM_HINT_Y - 5 : UI_GEOM.h - UI_GEOM.h / 6 - 1;
}

// The list area and the x range of its rows: inside the visible span of every row of the area (first start's
// lower list keeps the ROM list's width, as on the canvas).
static void list_box(const ui_menu_view_t *v, int *top, int *bottom, int *x0, int *x1)
{
    *top = list_top(v);
    *bottom = list_bottom(v);
    *x0 = 0;
    *x1 = UI_W - 1;
    ui_span(LIST_LAYOUT[v->page].top, *bottom, x0, x1);
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

static void hm_or_dash(int32_t s, char *out, size_t cap)
{
    if (s >= 0 && s < 86400) snprintf(out, cap, "%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60));
    else snprintf(out, cap, "-");
}

static void list_value(const ui_menu_view_t *v, int row, char *out, size_t cap)
{
    const st_settings_t *c = v->cfg;
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
    case UI_ROW_CLOCK: hm_or_dash(v->pet_s, out, cap); break;
    case UI_ROW_PANEL: snprintf(out, cap, "QR code"); break;
    case UI_ROW_PETS: snprintf(out, cap, "%s", v->running_name ? v->running_name : ""); break;
    case UI_ROW_RESET: snprintf(out, cap, "erase everything"); break;
    default: break;
    }
}

static const char *const LIST_NAMES[UI_ROW_COUNT] = { "Sound", "Brightness", "Dimming", "Palette", "Time zone",
                                                      "Pet clock", "Pets", "Web panel", "Factory reset" };

// ---- Rows of the list pages ----
int ui_menu_row_count(const ui_menu_view_t *v)
{
    switch (v->page) {
    case UI_PAGE_LIST: return UI_ROW_COUNT;
    case UI_PAGE_TZ_REGION: return ui_menu_region_at(0) ? s_region_n : 0;
    case UI_PAGE_TZ_CITY: return city_count(v->region);
    case UI_PAGE_ROM: return v->roms && v->rom_count > 0 ? v->rom_count : 0;
    case UI_PAGE_PETS: return v->pets ? v->pet_count + 1 : 0;  // the pets, then "+ New pet"
    default: return 0;
    }
}

int ui_menu_scroll_max(const ui_menu_view_t *v)
{
    if (!is_list(v->page)) return 0;
    int n = ui_menu_row_count(v), top, bottom, x0, x1;
    list_box(v, &top, &bottom, &x0, &x1);
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
    return list_top(v) + LIST_PAD + i * (LIST_LAYOUT[v->page].row_h + ROW_GAP) - scroll;
}

int ui_menu_row_at(const ui_menu_view_t *v, int x, int y)
{
    if (!is_list(v->page)) return -1;
    int top, bottom, x0, x1;
    list_box(v, &top, &bottom, &x0, &x1);
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
    return v->page != UI_PAGE_LIST && v->page != UI_PAGE_ROM_CONFIRM && v->page != UI_PAGE_CLOCK_CONFIRM &&
           v->page != UI_PAGE_PET_DELETE &&
           !(v->page == UI_PAGE_ROM && v->rom_first) &&
           y >= 0 && y < TITLE_Y && x >= 0 && x < UI_W;
}

// The row that is the current choice on the TZ and ROM pages, -1 if none.
static int current_row(const ui_menu_view_t *v)
{
    char r[32];
    if (v->page == UI_PAGE_ROM) return -1;
    if (v->page == UI_PAGE_PETS)
        return v->pets && v->pet_current >= 0 && v->pet_current < v->pet_count ? v->pet_current : -1;
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
    case UI_PAGE_ROM: return v->rom_first ? "Pick a pet" : "New pet";
    case UI_PAGE_ROM_CONFIRM: return "";  // drawn by draw_new_confirm
    case UI_PAGE_PETS: return "Pets";
    case UI_PAGE_PET_SHEET: return "";    // drawn by draw_pet_sheet
    case UI_PAGE_PET_DELETE: return "";
    case UI_PAGE_RESET: return "";         // drawn by draw_reset (two lines)
    case UI_PAGE_CLOCK: return "Pet clock";
    case UI_PAGE_CLOCK_CONFIRM: return "";  // drawn by draw_clock_confirm
    }
    return "";
}

static int title_y(const ui_menu_view_t *v)
{
    if (v->page == UI_PAGE_ROM && v->rom_first) return ROM_FIRST_TITLE_Y;
    return v->page == UI_PAGE_LIST ? LIST_TITLE_Y : TITLE_Y;
}

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
    int cur = is_tz(v->page) ? current_row(v) : -1;
    if (cur < 0) return false;
    int top, bottom, x0, x1, n0, n1;
    list_box(v, &top, &bottom, &x0, &x1);
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
    if (ui_menu_back_at(v, UI_W / 2, BACK_Y)) ui_text_centred_at(fb, BACK_Y, "< back", 1, c->soft);
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
    list_box(v, &top, &bottom, &x0, &x1);
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
            list_value(v, i, value, sizeof value);
            ui_draw_text_ellipsis(fb, tx0, tx1 - 7 - 4, y + 25, value, 1, fg);
            ui_draw_text(fb, tx1 - 7, y + 25, ">", 1, fg);
            continue;
        }
        if (v->page == UI_PAGE_PETS && i == v->pet_count) {  // "+ New pet": an outline, no fill
            ui_fill_rrect(fb, x0, y, x1 - x0 + 1, rh, ROW_RADIUS, c->ink);
            if (!hi) ui_fill_rrect(fb, x0 + 2, y + 2, x1 - x0 - 3, rh - 4, ROW_RADIUS - 1, c->bg);
            const char *t = "+ New pet";
            ui_draw_text(fb, x0 + (x1 - x0 + 1 - ui_text_width(t, 1)) / 2, y + (rh - 8) / 2, t, 1, hi ? c->bg : c->ink);
            continue;
        }
        if (v->page == UI_PAGE_ROM || v->page == UI_PAGE_PETS) {  // name over its line, the running pet checked
            const char *nm = v->page == UI_PAGE_ROM ? v->roms[i].name : v->pets[i].name;
            const char *sb = v->page == UI_PAGE_ROM ? v->roms[i].sub : v->pets[i].sub;
            if (v->page == UI_PAGE_ROM && !v->roms[i].enabled && !v->rom_first) {  // greyed: not on the card
                ui_fill_rrect(fb, x0, y, x1 - x0 + 1, rh, ROW_RADIUS, c->bg);
                fg = c->soft;
            }
            int t1 = i == cur ? tx1 - 14 - 4 : tx1;
            ui_draw_text_ellipsis(fb, tx0, t1, y + 4, nm ? nm : "", 2, fg);
            ui_draw_text_ellipsis(fb, tx0, t1, y + 23, sb ? sb : "", 1, fg);
            if (i == cur) ui_draw_bits(fb, tx1 - 14, y + 11, CHECK, 7, 6, 2, fg);
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
    if (v->panel_qr && v->panel_qr->size) {
        int x0, x1;
        ui_span(QR_Y, QR_Y + QR_PLATE - 1, &x0, &x1);
        int px = (x0 + x1 + 1 - QR_PLATE) / 2;
        uint16_t light = ui_rgb565(QR_LIGHT);
        ui_fill_rrect(fb, px, QR_Y, QR_PLATE, QR_PLATE, 3, light);
        ui_draw_qr(fb, px + QR_PLATE / 2, QR_Y + QR_PLATE / 2, QR_PLATE, v->panel_qr, ui_rgb565(QR_DARK), light);
    }
    // The address without "http://" and the trailing '/'.
    const char *u = strncmp(v->panel_url, "http://", 7) ? v->panel_url : v->panel_url + 7;
    char host[64];
    snprintf(host, sizeof host, "%.*s", (int)strcspn(u, "/"), u);
    ui_text_centred_at(fb, PANEL_ADDR_Y, host, 1, c->ink);
    if (v->panel_name) ui_text_centred_at(fb, PANEL_NAME_Y, v->panel_name, 1, c->soft);
}

// ---- ROM picker ----
#define PILL_W 84
#define PILL_H 36
#define PILL_GAP 4

static void draw_rom_list(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    draw_list(fb, v, c);
    if (!v->roms) return;  // not loaded yet
    int n = ui_menu_row_count(v);
    if (n == 0) {
        ui_text_centred_at(fb, ROM_EMPTY_Y, "No ROMs on the SD card", 1, c->ink);
        return;
    }
    if (!v->rom_first) return;  // the New pet list has no hint (canvas NewPet)
    // The hint sits under the rows (canvas PickerFirst), never lower than the list's own.
    int hint_y = ROM_HINT_Y, end = row_top(v, n - 1, clamp_scroll(v)) + LIST_LAYOUT[UI_PAGE_ROM].row_h - 1 + ROM_HINT_GAP;
    if (end < hint_y) hint_y = end;
    ui_text_centred_at(fb, hint_y, "tap one to start", 1, c->soft);
}

// The two pills, centred side by side: i 0 Cancel, 1 Switch.
static int pill_x(int i) { return UI_W / 2 - PILL_W - PILL_GAP / 2 + i * (PILL_W + PILL_GAP); }

// A pill button of height h: filled (ink, label in bg) or outlined (border b px, label in ink).
static void draw_pill_hb(uint16_t *fb, int x, int y, int w, int h, int b, const char *label, bool filled,
                         const colors_t *c)
{
    ui_fill_rrect(fb, x, y, w, h, h / 2, c->ink);
    if (!filled) ui_fill_rrect(fb, x + b, y + b, w - 2 * b, h - 2 * b, h / 2 - b, c->bg);
    ui_draw_text(fb, x + (w - ui_text_width(label, 1)) / 2, y + (h - 8) / 2, label, 1, filled ? c->bg : c->ink);
}
static void draw_pill(uint16_t *fb, int x, int y, int w, const char *label, bool filled, const colors_t *c)
{
    draw_pill_hb(fb, x, y, w, PILL_H, 2, label, filled, c);
}

// ---- New pet confirm (canvas NewPetConfirm) ----
#define NEW_TITLE_Y 48       // "New egg?", scale 2
#define NEW_BODY_Y 75        // four lines, 14 px apart
#define NEW_NOTE_Y 139       // "The ball restarts."
#define NEW_PILL_Y 158

static void draw_new_confirm(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    const ui_rom_row_t *r = v->roms && v->rom_pick >= 0 && v->rom_pick < v->rom_count ? &v->roms[v->rom_pick] : NULL;
    char line[4][48];
    snprintf(line[0], sizeof line[0], "%s hatches", v->new_name ? v->new_name : "A new pet");
    snprintf(line[1], sizeof line[1], "from %s.", r && r->file ? r->file : "its ROM");
    snprintf(line[2], sizeof line[2], "%s is saved and", v->running_name ? v->running_name : "Your pet");
    snprintf(line[3], sizeof line[3], "waits for you.");
    ui_text_centred_at(fb, NEW_TITLE_Y, "New egg?", 2, c->ink);
    for (int i = 0; i < 4; i++) ui_text_centred_at(fb, NEW_BODY_Y + 14 * i, line[i], 1, c->ink);
    ui_text_centred_at(fb, NEW_NOTE_Y, "The ball restarts.", 1, c->soft);
    draw_pill(fb, pill_x(0), NEW_PILL_Y, PILL_W, "Cancel", false, c);
    draw_pill(fb, pill_x(1), NEW_PILL_Y, PILL_W, "Start", true, c);
}

// ---- A pet's sheet and its delete confirm (canvas PetSheet, PetSheetRunning, PetDelete) ----
#define SHEET_NAME_Y 56      // scale 2
#define SHEET_LINE_Y 88      // two lines, 14 px apart
#define SHEET_PILL_Y 130     // Cancel / Switch
#define SHEET_OFF_Y 122      // "ROM not on" / "the SD card"
#define SHEET_OFF_PILL_Y 146 // Cancel alone (the ROM isn't on the card)
#define SHEET_CLOSE_Y 140    // the running pet's Close
#define SHEET_CLOSE_W 88
#define SHEET_HINT_Y 188     // "switch away" / "to delete it"
#define DEL_PILL_Y 186       // the small "Delete..." pill
#define DEL_PILL_W 90
#define DEL_PILL_H 24
#define DELETE_TITLE_Y 56    // "Delete" / "<name>?", scale 2, 22 px apart
#define DELETE_BODY_Y 109
#define DELETE_PILL_Y 150

static const ui_pet_row_t *picked(const ui_menu_view_t *v)
{
    return v->pets && v->pet_pick >= 0 && v->pet_pick < v->pet_count ? &v->pets[v->pet_pick] : NULL;
}
static bool picked_running(const ui_menu_view_t *v) { return v->pet_pick >= 0 && v->pet_pick == v->pet_current; }
static int centred_x(int w) { return (UI_W - w) / 2; }

static void draw_centred_name(uint16_t *fb, int y, const char *s, const colors_t *c)
{
    int x0, x1;
    ui_span(y, y + 15, &x0, &x1);
    x0 += TITLE_MARGIN;
    x1 -= TITLE_MARGIN;
    int tw = ui_text_width(s, 2), box = x1 - x0 + 1;
    if (tw <= box) ui_draw_text(fb, x0 + (box - tw) / 2, y, s, 2, c->ink);
    else ui_draw_text_ellipsis(fb, x0, x1 + 1, y, s, 2, c->ink);
}

static void draw_pet_sheet(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    const ui_pet_row_t *p = picked(v);
    if (!p) return;
    draw_centred_name(fb, SHEET_NAME_Y, p->name ? p->name : "", c);
    ui_text_centred_at(fb, SHEET_LINE_Y, p->model_age ? p->model_age : "", 1, c->ink);
    ui_text_centred_at(fb, SHEET_LINE_Y + 14, p->away ? p->away : "", 1, c->ink);
    if (picked_running(v)) {
        draw_pill(fb, centred_x(SHEET_CLOSE_W), SHEET_CLOSE_Y, SHEET_CLOSE_W, "Close", true, c);
        ui_text_centred_at(fb, SHEET_HINT_Y, "switch away", 1, c->soft);
        ui_text_centred_at(fb, SHEET_HINT_Y + 12, "to delete it", 1, c->soft);
        return;
    }
    if (p->can_switch) {
        draw_pill(fb, pill_x(0), SHEET_PILL_Y, PILL_W, "Cancel", false, c);
        draw_pill(fb, pill_x(1), SHEET_PILL_Y, PILL_W, "Switch", true, c);
    } else {
        ui_text_centred_at(fb, SHEET_OFF_Y, "ROM not on", 1, c->soft);
        ui_text_centred_at(fb, SHEET_OFF_Y + 12, "the SD card", 1, c->soft);
        draw_pill(fb, centred_x(SHEET_CLOSE_W), SHEET_OFF_PILL_Y, SHEET_CLOSE_W, "Cancel", false, c);
    }
    draw_pill_hb(fb, centred_x(DEL_PILL_W), DEL_PILL_Y, DEL_PILL_W, DEL_PILL_H, 1, "Delete...", false, c);
}

static void draw_pet_delete(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    const ui_pet_row_t *p = picked(v);
    char q[24];
    snprintf(q, sizeof q, "%s?", p && p->name ? p->name : "this pet");
    ui_text_centred_at(fb, DELETE_TITLE_Y, "Delete", 2, c->ink);
    draw_centred_name(fb, DELETE_TITLE_Y + 22, q, c);
    ui_text_centred_at(fb, DELETE_BODY_Y, "Its save is gone", 1, c->ink);
    ui_text_centred_at(fb, DELETE_BODY_Y + 14, "for good.", 1, c->ink);
    draw_pill(fb, pill_x(0), DELETE_PILL_Y, PILL_W, "Keep", true, c);
    draw_pill(fb, pill_x(1), DELETE_PILL_Y, PILL_W, "Delete", false, c);
}

static bool in_box(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

// ---- Factory reset (canvas Reset) ----
#define RESET_TITLE_Y 33     // "Factory" / "reset", scale 2, 22 px apart
#define RESET_BODY_Y 88      // three lines, 14 px apart
#define RESET_PILL_Y 145
#define RESET_PILL_W 172
#define RESET_HINT_Y 192

static void draw_reset(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    ui_text_centred_at(fb, RESET_TITLE_Y, "Factory", 2, c->ink);
    ui_text_centred_at(fb, RESET_TITLE_Y + 22, "reset", 2, c->ink);
    ui_text_centred_at(fb, RESET_BODY_Y, "Erases every pet,", 1, c->ink);
    ui_text_centred_at(fb, RESET_BODY_Y + 14, "setting and Wi-Fi.", 1, c->ink);
    ui_text_centred_at(fb, RESET_BODY_Y + 28, "ROMs on the card stay.", 1, c->ink);
    int x = (UI_W - RESET_PILL_W) / 2, pct = v->hold_pct < 0 ? 0 : v->hold_pct > 100 ? 100 : v->hold_pct;
    ui_fill_rrect(fb, x, RESET_PILL_Y, RESET_PILL_W, PILL_H, PILL_H / 2, c->ink);
    // Inside the border: ink up to the fill edge, bg after it, within the rounded inner shape only.
    const int iw = RESET_PILL_W - 4, ih = PILL_H - 4, r = ih / 2, ix = x + 2, iy = RESET_PILL_Y + 2;
    const int fill = iw * pct / 100;
    for (int j = 0; j < ih; j++)
        for (int i = 0; i < iw; i++) {
            int cx = i < r ? r - 1 - i : i >= iw - r ? i - (iw - r) : -1, cy = j < r ? r - 1 - j : j - (ih - r);
            if (cx >= 0 && cy >= 0 && (cx * 2 + 1) * (cx * 2 + 1) + (cy * 2 + 1) * (cy * 2 + 1) > 4 * r * r) continue;
            fb[(iy + j) * UI_W + ix + i] = i < fill ? c->ink : c->bg;
        }
    // The label in bg over the filled part and ink over the rest.
    const char *label = pct > 0 ? "Keep holding" : "Hold to erase";
    int lx = x + (RESET_PILL_W - ui_text_width(label, 1)) / 2, ly = RESET_PILL_Y + (PILL_H - 8) / 2;
    if (fill > 0) ui_draw_text_clipped(fb, ix, ix + fill, ly, ix - lx, label, 1, c->bg);
    ui_draw_text_clipped(fb, ix + fill, ix + iw, ly, ix + fill - lx, label, 1, c->ink);
    ui_text_centred_at(fb, RESET_HINT_Y, pct > 0 ? "lift to cancel" : "hold for 3 s", 1, c->soft);
}

// ---- Pet clock ----
#define CLOCK_PET_Y 62        // "Pet 09:58:00", scale 2
#define CLOCK_BALL_Y 86       // "Ball 14:32:05", scale 2
#define CLOCK_ZONE_Y 108      // the tz setting as it is, scale 1
#define CLOCK_NOTE_Y 130      // "Pet's clock menu is open" after a timed-out attempt
#define CLOCK_PILL_Y 148
#define CLOCK_PILL_W 150
#define CLOCK_PILL_X ((UI_W - CLOCK_PILL_W) / 2)
#define CLOCK_CONFIRM_Y 62    // "Set pet clock" / "to 14:32?", scale 2, 18 px apart
#define CLOCK_CONFIRM_ZONE_Y 102
#define CLOCK_WARN_Y 118      // three lines, 12 px apart
#define CLOCK_CONFIRM_PILL_Y 158

static void hms_or_dashes(int32_t s, char *out, size_t cap)
{
    if (s >= 0 && s < 86400) snprintf(out, cap, "%02d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    else snprintf(out, cap, "--:--");
}

static const char *clock_reason(ui_clock_why_t w)
{
    switch (w) {
    case UI_CLOCK_UNSUPPORTED: return "Not available for this pet";
    case UI_CLOCK_NO_TIME: return "Waiting for internet time";
    case UI_CLOCK_CATCHUP: return "Pet is catching up";
    case UI_CLOCK_PENDING: return "Setting the clock...";
    default: return NULL;
    }
}

static void draw_clock(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    char t[16], line[24];
    hms_or_dashes(v->pet_s, t, sizeof t);
    snprintf(line, sizeof line, "Pet %s", t);
    ui_text_centred_at(fb, CLOCK_PET_Y, line, 2, c->ink);
    hms_or_dashes(v->ball_s, t, sizeof t);
    snprintf(line, sizeof line, "Ball %s", t);
    ui_text_centred_at(fb, CLOCK_BALL_Y, line, 2, c->ink);
    ui_text_centred_at(fb, CLOCK_ZONE_Y, v->cfg->tz, 1, c->soft);
    if (v->clock_busy) ui_text_centred_at(fb, CLOCK_NOTE_Y, "Pet's clock menu is open", 1, c->soft);
    const char *why = clock_reason(v->clock_why);
    if (why) ui_text_centred_at(fb, CLOCK_PILL_Y + (PILL_H - 8) / 2, why, 1, c->soft);
    else draw_pill(fb, CLOCK_PILL_X, CLOCK_PILL_Y, CLOCK_PILL_W, "Set to ball time", true, c);
}

static void draw_clock_confirm(uint16_t *fb, const ui_menu_view_t *v, const colors_t *c)
{
    char line[24];
    int32_t s = v->confirm_s;
    if (s >= 0 && s < 86400) snprintf(line, sizeof line, "to %02d:%02d?", (int)(s / 3600), (int)(s / 60 % 60));
    else snprintf(line, sizeof line, "to the ball's time?");
    ui_text_centred_at(fb, CLOCK_CONFIRM_Y, "Set pet clock", 2, c->ink);
    ui_text_centred_at(fb, CLOCK_CONFIRM_Y + 18, line, 2, c->ink);
    ui_text_centred_at(fb, CLOCK_CONFIRM_ZONE_Y, v->cfg->tz, 1, c->soft);
    static const char *const WARN[3] = { "Your pet may wake up", "or fall asleep, and", "its age may change." };
    for (int i = 0; i < 3; i++) ui_text_centred_at(fb, CLOCK_WARN_Y + 12 * i, WARN[i], 1, c->ink);
    draw_pill(fb, pill_x(0), CLOCK_CONFIRM_PILL_Y, PILL_W, "Cancel", false, c);
    draw_pill(fb, pill_x(1), CLOCK_CONFIRM_PILL_Y, PILL_W, "Set", true, c);
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
    case UI_PAGE_ROM: draw_rom_list(fb, v, &c); break;
    case UI_PAGE_ROM_CONFIRM: draw_new_confirm(fb, v, &c); break;
    case UI_PAGE_PETS: draw_list(fb, v, &c); break;
    case UI_PAGE_PET_SHEET: draw_pet_sheet(fb, v, &c); break;
    case UI_PAGE_PET_DELETE: draw_pet_delete(fb, v, &c); break;
    case UI_PAGE_RESET: draw_reset(fb, v, &c); break;
    case UI_PAGE_CLOCK: draw_clock(fb, v, &c); break;
    case UI_PAGE_CLOCK_CONFIRM: draw_clock_confirm(fb, v, &c); break;
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
    case UI_PAGE_ROM_CONFIRM:
        for (int i = 0; i < 2; i++)
            if (in_box(x, y, pill_x(i), NEW_PILL_Y, PILL_W, PILL_H)) ctl = i ? UI_CTL_NEW_START : UI_CTL_NEW_CANCEL;
        break;
    case UI_PAGE_PET_SHEET: {
        const ui_pet_row_t *p = picked(v);
        if (!p) break;
        if (picked_running(v)) {
            if (in_box(x, y, centred_x(SHEET_CLOSE_W), SHEET_CLOSE_Y, SHEET_CLOSE_W, PILL_H)) ctl = UI_CTL_PET_CANCEL;
            break;
        }
        if (p->can_switch) {
            for (int i = 0; i < 2; i++)
                if (in_box(x, y, pill_x(i), SHEET_PILL_Y, PILL_W, PILL_H)) ctl = i ? UI_CTL_PET_SWITCH : UI_CTL_PET_CANCEL;
        } else if (in_box(x, y, centred_x(SHEET_CLOSE_W), SHEET_OFF_PILL_Y, SHEET_CLOSE_W, PILL_H)) {
            ctl = UI_CTL_PET_CANCEL;
        }
        if (in_box(x, y, centred_x(DEL_PILL_W), DEL_PILL_Y, DEL_PILL_W, DEL_PILL_H)) ctl = UI_CTL_PET_DELETE;
        break;
    }
    case UI_PAGE_RESET:
        if (in_box(x, y, (UI_W - RESET_PILL_W) / 2, RESET_PILL_Y, RESET_PILL_W, PILL_H)) ctl = UI_CTL_RESET_HOLD;
        break;
    case UI_PAGE_PET_DELETE:
        for (int i = 0; i < 2; i++)
            if (in_box(x, y, pill_x(i), DELETE_PILL_Y, PILL_W, PILL_H)) ctl = i ? UI_CTL_DEL_DELETE : UI_CTL_DEL_KEEP;
        break;
    case UI_PAGE_CLOCK:
        if (v->clock_why == UI_CLOCK_OK && x >= CLOCK_PILL_X && x < CLOCK_PILL_X + CLOCK_PILL_W && y >= CLOCK_PILL_Y &&
            y < CLOCK_PILL_Y + PILL_H)
            ctl = UI_CTL_CLOCK_SET;
        break;
    case UI_PAGE_CLOCK_CONFIRM:
        for (int i = 0; i < 2; i++)
            if (x >= pill_x(i) && x < pill_x(i) + PILL_W && y >= CLOCK_CONFIRM_PILL_Y && y < CLOCK_CONFIRM_PILL_Y + PILL_H)
                ctl = i ? UI_CTL_CLOCK_OK : UI_CTL_CLOCK_CANCEL;
        break;
    default: break;
    }
    if (ctl >= 0 && value_out) *value_out = val;
    return ctl;
}
