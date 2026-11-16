// SPDX-License-Identifier: GPL-3.0-or-later
// QR codes: draws a module grid the device encoded (espressif/qrcode in main/). No encoding here, so the
// portable ui component needs no ESP-IDF headers.
#include "ui.h"

#define QR_QUIET 2

int ui_draw_qr(uint16_t *fb, int cx, int cy, int max_px, const ui_qr_t *qr, uint16_t dark, uint16_t light)
{
    if (!qr || qr->size == 0 || qr->size > UI_QR_MAX) return 0;
    int size = qr->size, total = size + 2 * QR_QUIET;
    int m = max_px / total;
    if (m < 1) return 0;
    int side = total * m;
    int x0 = cx - side / 2, y0 = cy - side / 2;
    for (int y = 0; y < side; y++) {
        int py = y0 + y;
        if (py < 0 || py >= UI_H) continue;
        int my = y / m - QR_QUIET;
        for (int x = 0; x < side; x++) {
            int px = x0 + x;
            if (px < 0 || px >= UI_W) continue;
            int mx = x / m - QR_QUIET;
            bool on = mx >= 0 && my >= 0 && mx < size && my < size && qr->mod[my][mx];
            fb[py * UI_W + px] = on ? dark : light;
        }
    }
    return side;
}
