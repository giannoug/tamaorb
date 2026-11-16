// SPDX-License-Identifier: GPL-3.0-or-later
// Text screens: catch-up, low battery, ROM picker, messages. Layouts follow the design canvas
// artboards at device scale.
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_internal.h"

#define LINE_H 11

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

void ui_render_catchup(uint16_t *fb, uint64_t done_s, uint64_t total_s, uint32_t eta_s, const ui_palette_t *p)
{
    colors_t c = colors(p);
    clear(fb, c.bg);
    text_centred(fb, 52, "Catching up", 2, c.ink);
    text_centred(fb, 80, "Your pet lived on\nwhile the power was off", 1, c.soft);

    int filled = total_s ? (int)(done_s * 16 / total_s) : 0;
    if (filled > 16) filled = 16;
    const int x0 = 120 - (16 * 9 - 2) / 2;
    for (int i = 0; i < 16; i++) rect(fb, x0 + 9 * i, 150, 7, 12, i < filled ? c.ink : c.ghost);

    char line[48];
    snprintf(line, sizeof line, "%uh %02um of %uh %02um",
             (unsigned)(done_s / 3600), (unsigned)(done_s / 60 % 60),
             (unsigned)(total_s / 3600), (unsigned)(total_s / 60 % 60));
    text_centred(fb, 172, line, 1, c.ink);
    snprintf(line, sizeof line, "about %u min left", (unsigned)((eta_s + 59) / 60));
    text_centred(fb, 188, line, 1, c.soft);
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

void ui_render_picker(uint16_t *fb, const char *const *names, const char *const *subs,
                      int n, int selected, const ui_palette_t *p)
{
    colors_t c = colors(p);
    clear(fb, c.bg);
    text_centred(fb, 40, "Pick a pet", 2, c.ink);  // "Choose a pet" is wider than the circle at this height
    if (n > UI_PICKER_MAX) n = UI_PICKER_MAX;
    for (int i = 0; i < n; i++) {
        int top = UI_PICKER_TOP + i * UI_PICKER_STEP;
        bool sel = i == selected;
        if (sel) rect(fb, UI_PICKER_X, top, UI_PICKER_W, UI_PICKER_H, c.ink);
        else frame_rect(fb, UI_PICKER_X, top, UI_PICKER_W, UI_PICKER_H, 2, c.ink);
        uint16_t fg = sel ? c.bg : c.ink;
        ui_draw_text(fb, UI_PICKER_X + 8, top + 4, names[i], 2, fg);
        ui_draw_text(fb, UI_PICKER_X + 8, top + 21, subs[i], 1, fg);
    }
    text_centred(fb, 196, "A up  B pick  C down", 1, c.soft);
}
