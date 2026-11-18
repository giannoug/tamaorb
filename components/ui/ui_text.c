// SPDX-License-Identifier: GPL-3.0-or-later
// Text screens: catch-up, low battery, messages. Layouts follow the design canvas
// artboards at device scale.
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_internal.h"

#define LINE_H 11

void ui_fmt_away(uint64_t s, bool largest_only, char *out, size_t cap)
{
    static const struct { uint64_t secs; char unit; } U[] = { { 86400, 'd' }, { 3600, 'h' }, { 60, 'm' }, { 1, 's' } };
    int i = 0;
    while (i < 3 && s < U[i].secs) i++;
    unsigned long long a = s / U[i].secs, b = i < 3 ? s % U[i].secs / U[i + 1].secs : 0;
    if (largest_only || b == 0) snprintf(out, cap, "%llu%c", a, U[i].unit);
    else snprintf(out, cap, "%llu%c %llu%c", a, U[i].unit, b, U[i + 1].unit);
}

typedef struct { uint16_t bg, ink, ghost, soft; } colors_t;

static colors_t colors(const ui_palette_t *p)
{
    return (colors_t){ ui_rgb565(p->bg), ui_rgb565(p->ink),
                       ui_rgb565(ui_mix(p->bg, p->ink, p->ghost_pct)),
                       ui_rgb565(ui_mix(p->bg, p->ink, 80)) };
}

static void clear(uint16_t *fb, uint16_t bg) { ui_clear_rows(fb, 0, UI_H - 1, bg); }

static void rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c) { ui_fill_rect(fb, x, y, w, h, c); }

void ui_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    int i0 = x < 0 ? 0 : x, i1 = x + w > UI_W ? UI_W : x + w;
    int j0 = y < 0 ? 0 : y, j1 = y + h > UI_H ? UI_H : y + h;
    for (int j = j0; j < j1; j++) {
        uint16_t *row = fb + j * UI_W;
        for (int i = i0; i < i1; i++) row[i] = c;
    }
}

void ui_clear_rows(uint16_t *fb, int y0, int y1, uint16_t bg)
{
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < UI_H; y++) {
        int x0 = 0, x1 = -1;
        ui_geom_span(&UI_GEOM, y, y, &x0, &x1);
        uint16_t *row = fb + y * UI_W;
        int x = 0;
        for (; x < x0; x++) row[x] = 0x0000;
        for (; x <= x1; x++) row[x] = bg;
        for (; x < UI_W; x++) row[x] = 0x0000;
    }
}

// Inside the w x h box with corners of radius r (pixel centres, doubled to stay integer).
static bool in_rrect(int i, int j, int w, int h, int r)
{
    int cx = i < r ? r : (i >= w - r ? w - r : -1);
    int cy = j < r ? r : (j >= h - r ? h - r : -1);
    if (cx < 0 || cy < 0) return true;
    int dx = 2 * i + 1 - 2 * cx, dy = 2 * j + 1 - 2 * cy;
    return dx * dx + dy * dy <= 4 * r * r;
}

void ui_fill_rrect(uint16_t *fb, int x, int y, int w, int h, int r, uint16_t c)
{
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        // Each row is one run, symmetric about the middle: find where it starts.
        int i = 0;
        while (i < w && !in_rrect(i, j, w, h, r)) i++;
        if (i < w) ui_fill_rect(fb, x + i, y + j, w - 2 * i, 1, c);
    }
}

void ui_fill_disc(uint16_t *fb, int x, int y, int d, uint16_t c) { ui_fill_rrect(fb, x, y, d, d, d / 2, c); }

void ui_draw_bits(uint16_t *fb, int x, int y, const char *const *rows, int w, int h, int scale, uint16_t c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (rows[j][i] == '#') ui_fill_rect(fb, x + i * scale, y + j * scale, scale, scale, c);
}

int ui_span(int y0, int y1, int *x0, int *x1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > UI_GEOM.h - 1) y1 = UI_GEOM.h - 1;
    return ui_geom_span(&UI_GEOM, y0, y1, x0, x1);
}

int ui_text_centred_at(uint16_t *fb, int y, const char *s, uint8_t scale, uint16_t color)
{
    int x0 = 0, x1 = UI_W - 1;
    ui_span(y, y + 8 * scale - 1, &x0, &x1);
    int w = ui_text_width(s, scale);
    if (w > x1 - x0 + 1) {  // wider than the span: cut with an ellipsis, never past the visible area
        ui_draw_text_ellipsis(fb, x0, x1 + 1, y, s, scale, color);
        return x0;
    }
    int x = (x0 + x1 + 1 - w) / 2;
    ui_draw_text(fb, x, y, s, scale, color);
    return x;
}

static void frame_rect(uint16_t *fb, int x, int y, int w, int h, int t, uint16_t c)
{
    rect(fb, x, y, w, t, c);
    rect(fb, x, y + h - t, w, t, c);
    rect(fb, x, y, t, h, c);
    rect(fb, x + w - t, y, t, h, c);
}

int ui_text_width(const char *line, uint8_t scale)
{
    int n = 0;
    while (line[n] && line[n] != '\n') n++;
    return n ? (8 * n - 1) * scale : 0;
}

int ui_text_fit(const char *s, uint8_t scale, int max_w)
{
    int n = 0;
    while (s[n] && s[n] != '\n' && (8 * (n + 1) - 1) * scale <= max_w) n++;
    return n;
}

// First n chars of s (stopping at '\n'), starting at x0 - dx, only inside [x0,x1).
static void draw_clipped_n(uint16_t *fb, int x0, int x1, int y, int dx, const char *s, int n,
                           uint8_t scale, uint16_t color)
{
    for (int i = 0; i < n && s[i] && s[i] != '\n'; i++)
        ui_draw_glyph_clip(fb, x0 - dx + i * 8 * scale, y, s[i], scale, color, x0, x1);
}

int ui_draw_text_clipped(uint16_t *fb, int x0, int x1, int y, int dx, const char *s, uint8_t scale, uint16_t color)
{
    draw_clipped_n(fb, x0, x1, y, dx, s, 1 << 20, scale, color);
    return ui_text_width(s, scale);
}

int ui_draw_text_ellipsis(uint16_t *fb, int x0, int x1, int y, const char *s, uint8_t scale, uint16_t color)
{
    int box = x1 - x0, tw = ui_text_width(s, scale);
    if (tw <= box) {
        draw_clipped_n(fb, x0, x1, y, 0, s, 1 << 20, scale, color);
        return tw;
    }
    int n = ui_text_fit(s, scale, box - 8 * scale);  // leave a cell for the ellipsis
    draw_clipped_n(fb, x0, x1, y, 0, s, n, scale, color);
    ui_draw_glyph_clip(fb, x0 + n * 8 * scale, y, UI_ELLIPSIS[0], scale, color, x0, x1);
    return (8 * (n + 1) - 1) * scale;
}

int ui_marquee_offset(int text_w, int box_w, uint32_t t_ms)
{
    int overflow = text_w - box_w;
    if (overflow <= 0) return 0;
    const uint32_t pause = 1000, px_per_s = 30;
    uint32_t scroll = ((uint32_t)overflow * 1000 + px_per_s - 1) / px_per_s;  // ms to cover the overflow
    uint32_t t = t_ms % (2 * (pause + scroll));
    int off;
    if (t < pause) off = 0;
    else if (t < pause + scroll) off = (int)((t - pause) * px_per_s / 1000);
    else if (t < 2 * pause + scroll) off = overflow;
    else off = overflow - (int)((t - 2 * pause - scroll) * px_per_s / 1000);
    return off < 0 ? 0 : off > overflow ? overflow : off;
}

int ui_draw_text(uint16_t *fb, int x, int y, const char *s, uint8_t scale, uint16_t color)
{
    int width = ui_text_width(s, scale);
    for (int cx = x; *s; s++) {
        if (*s == '\n') { y += LINE_H * scale; cx = x; continue; }
        ui_draw_glyph(fb, cx, y, *s, scale, color);
        cx += 8 * scale;
    }
    return width;
}

// Each line centred on x = 120. Returns the y below the last line.
static int text_centred(uint16_t *fb, int y, const char *s, uint8_t scale, uint16_t color)
{
    char line[64];
    while (*s) {
        size_t n = strcspn(s, "\n");
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, s, n);
        line[n] = '\0';
        ui_draw_text(fb, 120 - (ui_text_width(line, scale) + 1) / 2, y, line, scale, color);
        y += LINE_H * scale;
        s += n;
        if (*s == '\n') s++;
    }
    return y;
}

void ui_render_message(uint16_t *fb, const char *title, const char *body, const ui_palette_t *p)
{
    colors_t c = colors(p);
    clear(fb, c.bg);
    frame_rect(fb, 106, 62, 28, 28, 3, c.ink);
    text_centred(fb, 68, "!", 2, c.ink);
    text_centred(fb, 104, title, 2, c.ink);
    text_centred(fb, 132, body, 1, c.soft);
}

// ---- Pills (as the settings pages draw them) ----
#define PILL_W 84
#define PILL_H 36
#define PILL_GAP 4
static int pill_x(int i) { return UI_W / 2 - PILL_W - PILL_GAP / 2 + i * (PILL_W + PILL_GAP); }
static void pill(uint16_t *fb, int x, int y, int w, int h, int border, const char *label, bool filled, const colors_t *c)
{
    ui_fill_rrect(fb, x, y, w, h, h / 2, c->ink);
    if (!filled) ui_fill_rrect(fb, x + border, y + border, w - 2 * border, h - 2 * border, h / 2 - border, c->bg);
    uint16_t fg = filled ? c->bg : c->ink;
    int tw = ui_text_width(label, 1), ty = y + (h - 8) / 2;
    if (tw <= w - h) ui_draw_text(fb, x + (w - tw) / 2, ty, label, 1, fg);
    else ui_draw_text_ellipsis(fb, x + h / 2, x + w - h / 2, ty, label, 1, fg);  // clear of the rounded ends
}

#define NAME_MARGIN 5  // px kept free at each end of the visible span

// "<name><suffix>" centred on the visible span at y. When it wouldn't fit, the name alone is cut with an
// ellipsis, so the suffix's words still show.
static void name_line(uint16_t *fb, int y, const char *name, const char *suffix, uint8_t scale, uint16_t color)
{
    int x0 = 0, x1 = UI_W - 1;
    ui_span(y, y + 8 * scale - 1, &x0, &x1);
    x0 += NAME_MARGIN;
    x1 -= NAME_MARGIN;
    int box = x1 - x0 + 1;
    char line[64];
    snprintf(line, sizeof line, "%s%s", name, suffix);
    int tw = ui_text_width(line, scale);
    if (tw <= box) {
        ui_draw_text(fb, x0 + (box - tw) / 2, y, line, scale, color);
        return;
    }
    int sw = *suffix ? ui_text_width(suffix, scale) + scale : 0;  // + the gap column before the suffix
    int nbox = box - sw, n = ui_text_fit(name, scale, nbox - 8 * scale);
    int nw = (8 * (n + 1) - 1) * scale;  // what ui_draw_text_ellipsis draws: n chars and the ellipsis
    int x = x0 + (box - nw - sw) / 2;
    ui_draw_text_ellipsis(fb, x, x + nbox, y, name, scale, color);
    if (*suffix) ui_draw_text(fb, x + nw + scale, y, suffix, scale, color);
}
static bool in_box(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

// ---- Catching up, with Skip (canvas CatchupSkip, CatchupStop) ----
#define CU_TITLE_Y 42
#define CU_BODY_Y 68        // two lines, 13 px apart
#define CU_BAR_Y 105
#define CU_DONE_Y 127
#define CU_LEFT_Y 140
#define CU_SKIP_Y 170
#define CU_SKIP_W 80
#define CU_SKIP_H 28
#define STOP_TITLE_Y 60
#define STOP_BODY_Y 88      // three lines, 14 px apart
#define STOP_PILL_Y 145
#define STOP_NOTE_Y 192

void ui_render_catchup(uint16_t *fb, const char *name, uint64_t done_s, uint64_t total_s, uint32_t eta_s,
                       ui_skip_t skip, const ui_palette_t *p)
{
    colors_t c = colors(p);
    clear(fb, c.bg);
    char line[48], a[16], b[16];
    if (!name || !*name) name = "Your pet";
    ui_fmt_away(done_s, false, a, sizeof a);
    if (skip == UI_SKIP_CONFIRM) {
        text_centred(fb, STOP_TITLE_Y, "Stop here?", 2, c.ink);
        name_line(fb, STOP_BODY_Y, name, " goes on from", 1, c.ink);
        snprintf(line, sizeof line, "%s in. Its clock", a);
        text_centred(fb, STOP_BODY_Y + 14, line, 1, c.ink);
        text_centred(fb, STOP_BODY_Y + 28, "is set to now.", 1, c.ink);
        pill(fb, pill_x(0), STOP_PILL_Y, PILL_W, PILL_H, 2, "Go on", false, &c);
        pill(fb, pill_x(1), STOP_PILL_Y, PILL_W, PILL_H, 2, "Stop", true, &c);
        text_centred(fb, STOP_NOTE_Y, "still catching up", 1, c.soft);
        return;
    }
    text_centred(fb, CU_TITLE_Y, "Catching up", 2, c.ink);
    name_line(fb, CU_BODY_Y, name, " lives through", 1, c.soft);
    text_centred(fb, CU_BODY_Y + 13, "the time it was away", 1, c.soft);

    int filled = total_s ? (int)(done_s * 16 / total_s) : 0;
    if (filled > 16) filled = 16;
    const int x0 = 120 - (16 * 9 - 2) / 2;
    for (int i = 0; i < 16; i++) rect(fb, x0 + 9 * i, CU_BAR_Y, 7, 12, i < filled ? c.ink : c.ghost);

    ui_fmt_away(total_s, false, b, sizeof b);
    snprintf(line, sizeof line, "%s of %s", a, b);
    text_centred(fb, CU_DONE_Y, line, 1, c.ink);
    snprintf(line, sizeof line, "about %u min left", (unsigned)((eta_s + 59) / 60));
    text_centred(fb, CU_LEFT_Y, line, 1, c.soft);
    pill(fb, (UI_W - CU_SKIP_W) / 2, CU_SKIP_Y, CU_SKIP_W, CU_SKIP_H, 2, "Skip", false, &c);
}

ui_cu_hit_t ui_catchup_at(ui_skip_t skip, int x, int y)
{
    if (skip == UI_SKIP_CONFIRM) {
        if (in_box(x, y, pill_x(0), STOP_PILL_Y, PILL_W, PILL_H)) return UI_CU_GO_ON;
        if (in_box(x, y, pill_x(1), STOP_PILL_Y, PILL_W, PILL_H)) return UI_CU_STOP;
        return UI_CU_NONE;
    }
    return in_box(x, y, (UI_W - CU_SKIP_W) / 2, CU_SKIP_Y, CU_SKIP_W, CU_SKIP_H) ? UI_CU_SKIP : UI_CU_NONE;
}

// ---- The resume question (canvas Resume, ResumeBack): everything moves up 6 px when Back shows ----
#define RQ_NAME_Y 42
#define RQ_AWAY_Y 64
#define RQ_BODY_Y 84        // four lines, 13 px apart
#define RQ_PILL_Y 146
#define RQ_BACK_DY 6
#define RQ_BACK_Y 180
#define RQ_BACK_W 144
#define RQ_BACK_H 24

void ui_render_resume(uint16_t *fb, const ui_resume_view_t *v, const ui_palette_t *p)
{
    colors_t c = colors(p);
    clear(fb, c.bg);
    int up = v->back_name ? RQ_BACK_DY : 0;
    char line[48], d[16];
    name_line(fb, RQ_NAME_Y - up, v->name && *v->name ? v->name : "Your pet", "", 2, c.ink);
    ui_fmt_away(v->gap_s, false, d, sizeof d);
    snprintf(line, sizeof line, "was away %s", d);
    text_centred(fb, RQ_AWAY_Y - up, line, 1, c.ink);
    ui_fmt_away(v->ff_s, false, d, sizeof d);
    text_centred(fb, RQ_BODY_Y - up, "Fast-forward lives", 1, c.soft);
    snprintf(line, sizeof line, "through it (~%s).", d);
    text_centred(fb, RQ_BODY_Y + 13 - up, line, 1, c.soft);
    text_centred(fb, RQ_BODY_Y + 26 - up, "Resume picks up", 1, c.soft);
    text_centred(fb, RQ_BODY_Y + 39 - up, "where it left off.", 1, c.soft);
    pill(fb, pill_x(0), RQ_PILL_Y - 2 * up, PILL_W, PILL_H, 2, "Resume", false, &c);
    pill(fb, pill_x(1), RQ_PILL_Y - 2 * up, PILL_W, PILL_H, 2, "Fast-fwd", true, &c);
    if (v->back_name) {
        snprintf(line, sizeof line, "Back to %s", v->back_name);
        int x0 = 0, x1 = UI_W - 1, w = ui_text_width(line, 1) + 24;
        ui_span(RQ_BACK_Y, RQ_BACK_Y + RQ_BACK_H - 1, &x0, &x1);  // the widest the glass shows on those rows
        if (w < RQ_BACK_W) w = RQ_BACK_W;
        if (w > x1 - x0 + 1) w = x1 - x0 + 1;
        pill(fb, (UI_W - w) / 2, RQ_BACK_Y, w, RQ_BACK_H, 1, line, false, &c);
    }
}

ui_resume_hit_t ui_resume_at(const ui_resume_view_t *v, int x, int y)
{
    int up = v->back_name ? RQ_BACK_DY : 0;
    if (in_box(x, y, pill_x(0), RQ_PILL_Y - 2 * up, PILL_W, PILL_H)) return UI_RESUME_RESUME;
    if (in_box(x, y, pill_x(1), RQ_PILL_Y - 2 * up, PILL_W, PILL_H)) return UI_RESUME_FF;
    if (v->back_name && in_box(x, y, 20, RQ_BACK_Y, UI_W - 40, RQ_BACK_H)) return UI_RESUME_BACK;
    return UI_RESUME_NONE;
}

void ui_render_low_battery(uint16_t *fb, const ui_palette_t *p)
{
    colors_t c = colors(p);
    clear(fb, c.bg);
    frame_rect(fb, 82, 60, 75, 35, 3, c.ink);  // battery body
    rect(fb, 157, 71, 5, 14, c.ink);           // terminal nub
    rect(fb, 88, 66, 7, 23, c.ink);            // nearly empty charge
    text_centred(fb, 124, "Battery low", 2, c.ink);
    text_centred(fb, 150, "Pet saved.\nPlug in USB to wake.", 1, c.soft);
}
