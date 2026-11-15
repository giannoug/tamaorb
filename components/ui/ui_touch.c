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
