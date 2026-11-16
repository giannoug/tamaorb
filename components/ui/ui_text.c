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

static void clear(uint16_t *fb, uint16_t bg)
{
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++) fb[y * UI_W + x] = ui_in_circle(x, y) ? bg : 0x0000;
}

static void rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < UI_W && j >= 0 && j < UI_H) fb[j * UI_W + i] = c;
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
