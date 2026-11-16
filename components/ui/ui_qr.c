// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "ui.h"
#include "qrcodegen.h"

#define QR_MAX_VERSION 4
#define QR_QUIET 2

// Static buffers (no heap). Callers are the single UI task.
static uint8_t s_tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
static uint8_t s_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];

int ui_draw_qr(uint16_t *fb, int cx, int cy, int max_px, const char *text, uint16_t dark, uint16_t light)
{
    size_t n = strlen(text);
    if (n > sizeof s_tmp) return 0;
    memcpy(s_tmp, text, n);
    if (!qrcodegen_encodeBinary(s_tmp, n, s_qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, QR_MAX_VERSION,
                                qrcodegen_Mask_AUTO, true))
        return 0;
    int size = qrcodegen_getSize(s_qr);
    int total = size + 2 * QR_QUIET;
    int m = max_px / total;
    if (m < 1) return 0;
    int side = total * m;
    int x0 = cx - side / 2, y0 = cy - side / 2;
    for (int y = 0; y < side; y++) {
        int py = y0 + y;
        if (py < 0 || py >= UI_H) continue;
        for (int x = 0; x < side; x++) {
            int px = x0 + x;
            if (px < 0 || px >= UI_W) continue;
            int mx = x / m - QR_QUIET, my = y / m - QR_QUIET;
            bool on = mx >= 0 && my >= 0 && mx < size && my < size && qrcodegen_getModule(s_qr, mx, my);
            fb[py * UI_W + px] = on ? dark : light;
        }
    }
    return side;
}
