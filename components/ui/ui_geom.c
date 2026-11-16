// SPDX-License-Identifier: GPL-3.0-or-later
// Screen geometry, so layouts never hard-code a round screen.
#include "ui.h"

const ui_geom_t UI_GEOM = { UI_W, UI_H, true };

static unsigned isqrt(unsigned n)  // floor(sqrt(n))
{
    unsigned r = 0, b = 1u << 30;
    while (b > n) b >>= 2;
    while (b) {
        if (n >= r + b) { n -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

// Visible x range of row y; false if the row is off screen or empty. Round screens are the circle
// inscribed in the w x h area (pixel centres, as ui_in_circle).
static bool row_range(const ui_geom_t *g, int y, int *x0, int *x1)
{
    if (y < 0 || y >= g->h) return false;
    if (!g->round) { *x0 = 0; *x1 = g->w - 1; return true; }
    int d = g->w < g->h ? g->w : g->h;  // circle diameter
    int dy = 2 * y + 1 - g->h;
    int m = d * d - dy * dy;
    if (m < 0) return false;
    // Pixel x is inside when |2x + 1 - w| <= s, s = floor(sqrt(m)): solve for x instead of scanning the row.
    int s = (int)isqrt((unsigned)m);
    int lo = (g->w - 1 - s + 1) >> 1, hi = (g->w - 1 + s) >> 1;  // ceil and floor of (w - 1 -+ s) / 2
    if (lo < 0) lo = 0;
    if (hi > g->w - 1) hi = g->w - 1;
    if (lo > hi) return false;
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
