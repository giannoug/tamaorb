// SPDX-License-Identifier: GPL-3.0-or-later
// Gesture classifier: pet buttons (via ui_touch_track), edge swipes, and menu taps/drags.
#include "ui.h"
#include <stdlib.h>

enum { PH_IDLE, PH_PET, PH_BAND, PH_MENU, PH_INERT };

static int absi(int v) { return v < 0 ? -v : v; }

void ui_gesture_set_geom(ui_gesture_t *g, const ui_geom_t *geom)
{
    *g = (ui_gesture_t){ 0 };
    g->geom = geom;
}

void ui_gesture_reset(ui_gesture_t *g)
{
    const ui_geom_t *geom = g->geom;
    *g = (ui_gesture_t){ 0 };
    g->geom = geom;
    g->fresh = true;
}

// Completes one swipe for a touch that began in a band (pt[0] only); the rest of the touch is then inert.
static bool swipe_check(ui_gesture_t *g, const ui_geom_t *geom, const ui_touch_t *t, ui_gevent_t *ev)
{
    if (!g->side) return false;
    int dx = t->pt[0].x - g->sx, dy = t->pt[0].y - g->sy;
    int inward = g->side == 1 ? dy : -dy;
    if (inward < UI_SWIPE_DIST_FOR(geom) || absi(dx) >= absi(dy)) return false;
    ev->kind = g->side == 1 ? UI_G_SWIPE_DOWN : UI_G_SWIPE_UP;
    ev->x = t->pt[0].x;
    ev->y = t->pt[0].y;
    ev->dx = (int16_t)dx;
    ev->dy = (int16_t)dy;
    g->phase = PH_INERT;  // one swipe per touch
    return true;
}

static uint8_t feed(ui_gesture_t *g, ui_gmode_t mode, const ui_touch_t *t, ui_gevent_t *ev)
{
    const ui_geom_t *geom = g->geom ? g->geom : &UI_GEOM;
    bool down = t->count > 0;

    if (!down) {
        uint8_t out = 0;
        if (g->phase == PH_PET) out = ui_touch_track(&g->tr, t);  // lets the tracker see the release
        if (g->phase == PH_MENU) {
            if (g->moved) {
                ev->kind = UI_G_DRAG;
                ev->x = g->lx;
                ev->y = g->ly;
                ev->released = true;
            } else {
                ev->kind = UI_G_TAP;
                ev->x = g->sx;
                ev->y = g->sy;
                ev->released = true;
            }
        }
        g->tr.down = false;
        g->phase = PH_IDLE;
        g->fresh = false;
        return out;
    }

    if (g->phase == PH_IDLE) {
        g->mode = mode;
        if (g->fresh) {  // a touch already held when we were reset
            g->phase = PH_INERT;
            g->fresh = false;
            return 0;
        }
        int n = t->count > 2 ? 2 : t->count;
        int band = UI_EDGE_BAND_FOR(geom);
        bool in_band = false;
        for (int i = 0; i < n; i++)
            if (t->pt[i].y < band || t->pt[i].y >= geom->h - band) in_band = true;
        g->sx = g->lx = t->pt[0].x;
        g->sy = g->ly = t->pt[0].y;
        g->moved = false;
        g->emitted = false;
        // only a touch whose first point (pt[0]) starts in a band can swipe, and its direction comes from pt[0]
        g->side = g->sy < band ? 1 : (g->sy >= geom->h - band ? 2 : 0);
        // left/right band: pet touches starting there are inert like a band touch that never swipes (menus keep taps)
        if (g->sx < band || g->sx >= geom->w - band) in_band = true;
        g->phase = mode == UI_GMODE_MENU ? PH_MENU : (in_band ? PH_BAND : PH_PET);
    } else if (g->phase != PH_INERT && g->mode != mode) {
        g->phase = PH_INERT;  // mode changed mid-touch: inert until release
    }

    switch (g->phase) {
    case PH_PET:
        return ui_touch_track(&g->tr, t);
    case PH_BAND:
        swipe_check(g, geom, t, ev);
        return 0;
    case PH_MENU: {
        if (swipe_check(g, geom, t, ev)) return 0;
        int x = t->pt[0].x, y = t->pt[0].y;
        if (!g->moved && (absi(x - g->sx) >= UI_TAP_SLOP || absi(y - g->sy) >= UI_TAP_SLOP)) g->moved = true;
        if (g->moved && (x != g->lx || y != g->ly)) {
            ev->kind = UI_G_DRAG;
            ev->x = (int16_t)x;
            ev->y = (int16_t)y;
            ev->dx = (int16_t)(x - g->lx);
            ev->dy = (int16_t)(y - g->ly);
        }
        g->lx = (int16_t)x;
        g->ly = (int16_t)y;
        return 0;
    }
    default:
        return 0;
    }
}

uint8_t ui_gesture_feed(ui_gesture_t *g, ui_gmode_t mode, const ui_touch_t *t, uint32_t now_ms, ui_gevent_t *ev)
{
    (void)now_ms;
    *ev = (ui_gevent_t){ .kind = UI_G_NONE };
    uint8_t out = feed(g, mode, t, ev);
    if (ev->kind != UI_G_NONE) {  // every event carries its touch's start point (g->sx/sy survive the release)
        ev->sx = g->sx;
        ev->sy = g->sy;
        ev->first = !g->emitted;
        g->emitted = true;
    }
    return out;
}
