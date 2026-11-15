// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.h"

uint8_t ui_touch_buttons(const ui_touch_t *t)
{
    uint8_t mask = 0;
    int n = t->count > 2 ? 2 : t->count;
    for (int i = 0; i < n; i++) {
        int x = t->pt[i].x;
        mask |= TC_BTN_MASK(x < 80 ? TC_BTN_A : (x < 160 ? TC_BTN_B : TC_BTN_C));
    }
    return mask;
}

static uint8_t zone_mask(int x) { return TC_BTN_MASK(x < 80 ? TC_BTN_A : (x < 160 ? TC_BTN_B : TC_BTN_C)); }

uint8_t ui_touch_track(ui_touch_tracker_t *tr, const ui_touch_t *t)
{
    if (t->count == 0) {
        tr->down = false;
        return 0;
    }
    if (t->count >= 2) {
        tr->down = false;  // when one finger lifts, the remaining one starts a fresh press
        return ui_touch_buttons(t);  // the chip separated two fingers
    }

    uint8_t cur = zone_mask(t->pt[0].x);
    if (!tr->down) {
        tr->down = true;
        tr->start_mask = cur;
    }
    const uint8_t a = TC_BTN_MASK(TC_BTN_A), c = TC_BTN_MASK(TC_BTN_C);
    if ((tr->start_mask == a && cur == c) || (tr->start_mask == c && cur == a)) return a | c;
    return tr->start_mask;
}
