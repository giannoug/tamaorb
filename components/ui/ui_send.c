// SPDX-License-Identifier: GPL-3.0-or-later
// Send the panel only the rows that changed since the last frame: a slider drag or a pet frame touches a few
// dozen rows, and on SPI every row not sent is time saved. Rows are compared by a 64-bit hash kept per row:
// the framebuffer sits in slow external RAM, so reading it once beats comparing it with a second copy.
#include "ui.h"

#define MERGE_GAP 4  // unchanged rows this close together are sent anyway: one transfer beats two set-ups

static uint64_t row_hash(const uint16_t *row)
{
    // Two independent 32-bit lanes (FNV-1a style and a Murmur-style multiply): a changed row slips through only
    // if both collide, about once in 2^64.
    const uint32_t *w = (const uint32_t *)row;
    uint32_t a = 2166136261u, b = 0x9E3779B9u;
    for (int i = 0; i < UI_W / 2; i++) {
        a = (a ^ w[i]) * 16777619u;
        b = (b + w[i]) * 2246822519u;
        b ^= b >> 15;
    }
    return ((uint64_t)a << 32) | b;
}

void ui_sent_init(ui_sent_t *s) { s->valid = false; }

void ui_sent_forget(ui_sent_t *s) { s->valid = false; }

void ui_send_changed(ui_sent_t *s, const uint16_t *fb, ui_send_fn send, void *ctx)
{
    int run0 = -1, last = -1;  // the open run starts at run0; last is its last changed row
    for (int y = 0; y < UI_H; y++) {
        uint64_t h = row_hash(fb + y * UI_W);
        if (s->valid && h == s->hash[y]) continue;
        s->hash[y] = h;
        if (run0 >= 0 && y - last > MERGE_GAP + 1) {
            send(ctx, fb, run0, last + 1);
            run0 = -1;
        }
        if (run0 < 0) run0 = y;
        last = y;
    }
    if (run0 >= 0) send(ctx, fb, run0, last + 1);
    s->valid = true;
}
