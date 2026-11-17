// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.h"

#define BTN_DX 56   // A and C sit this far left and right of B, on B's arc

static int strip_top(void) { return UI_LCD_Y + 16 * UI_LCD_PITCH + 2; }
static int strip_bottom(void) { return UI_H - UI_EDGE_BAND - 1; }

static int isqrt(int v)  // nearest integer square root, v >= 0
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return (v - r * r > (r + 1) * (r + 1) - v) ? r + 1 : r;
}

// The keys sit on an arc concentric with the screen: B at the bottom of the strip, A and C BTN_DX to either side.
void ui_button_centre(int btn, int *x, int *y)
{
    const int cx = UI_W / 2, cy = UI_H / 2, ra = strip_bottom() - UI_BTN_R - cy;
    const int dx = btn == TC_BTN_A ? -BTN_DX : btn == TC_BTN_C ? BTN_DX : 0;
    *x = cx + dx;
    *y = cy + isqrt(ra * ra - dx * dx);
}

// Split halfway between the button centres: x < 92 A, 92..147 B, >= 148 C on 240 px.
static int button_for_x(int x)
{
    if (x < UI_W / 2 - BTN_DX / 2) return TC_BTN_A;
    if (x < UI_W / 2 + BTN_DX / 2) return TC_BTN_B;
    return TC_BTN_C;
}

int ui_button_at(int x, int y)
{
    if (y < strip_top() || y > strip_bottom()) return -1;
    return button_for_x(x);
}

uint8_t ui_touch_buttons(const ui_touch_t *t)
{
    uint8_t mask = 0;
    int n = t->count > 2 ? 2 : t->count;
    for (int i = 0; i < n; i++) {
        int b = ui_button_at(t->pt[i].x, t->pt[i].y);
        if (b >= 0) mask |= TC_BTN_MASK(b);
    }
    return mask;
}

uint8_t ui_touch_track(ui_touch_tracker_t *tr, const ui_touch_t *t)
{
    if (t->count == 0) {
        tr->down = false;
        return 0;
    }
    if (t->count >= 2) {
        if (tr->down && tr->start_mask == 0) return 0;  // a touch that started above the strip stays inert till lift
        tr->down = false;  // when one finger lifts, the remaining one starts a fresh press
        return ui_touch_buttons(t);  // the chip separated two fingers
    }

    if (!tr->down) {
        tr->down = true;
        int b = ui_button_at(t->pt[0].x, t->pt[0].y);
        tr->start_mask = b < 0 ? 0 : TC_BTN_MASK(b);  // 0: started above the strip, inert until lift
    }
    const uint8_t cur = TC_BTN_MASK(button_for_x(t->pt[0].x));
    const uint8_t a = TC_BTN_MASK(TC_BTN_A), c = TC_BTN_MASK(TC_BTN_C);
    if ((tr->start_mask == a && cur == c) || (tr->start_mask == c && cur == a)) return a | c;
    return tr->start_mask;
}
