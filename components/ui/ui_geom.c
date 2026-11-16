// SPDX-License-Identifier: GPL-3.0-or-later
// Screen geometry, so layouts never hard-code a round screen.
#include "ui.h"

const ui_geom_t UI_GEOM = { UI_W, UI_H, true };

// Visible x range of row y; false if the row is off screen or empty. Round screens are the circle
// inscribed in the w x h area (pixel centres, as ui_in_circle).
static bool row_range(const ui_geom_t *g, int y, int *x0, int *x1)
{
    if (y < 0 || y >= g->h) return false;
    if (!g->round) { *x0 = 0; *x1 = g->w - 1; return true; }
    int d = g->w < g->h ? g->w : g->h;  // circle diameter
    int dy = 2 * y + 1 - g->h;
    int r2 = d * d;
    int lo = -1, hi = -1;
    for (int x = 0; x < g->w; x++) {
        int dx = 2 * x + 1 - g->w;
        if (dx * dx + dy * dy <= r2) { if (lo < 0) lo = x; hi = x; }
    }
    if (lo < 0) return false;
    *x0 = lo;
    *x1 = hi;
    return true;
}

int ui_geom_span(const ui_geom_t *g, int y0, int y1, int *x0, int *x1)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    int a0, a1, b0, b1;
    // The visible region is convex, so the end rows bound every row between them.
    if (!row_range(g, y0, &a0, &a1) || !row_range(g, y1, &b0, &b1)) return 0;
    int lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
    if (hi < lo) return 0;
    if (x0) *x0 = lo;
    if (x1) *x1 = hi;
    return hi - lo + 1;
}

void ui_geom_safe(const ui_geom_t *g, int *x, int *y, int *w, int *h)
{
    int sw = g->w, sh = g->h;
    if (g->round) {
        int d = g->w < g->h ? g->w : g->h;
        sw = sh = d * 707 / 1000;  // d / sqrt(2), rounded down
    }
    *w = sw;
    *h = sh;
    *x = (g->w - sw) / 2;
    *y = (g->h - sh) / 2;
}
