// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "remote.h"

const char *const RM_ICON_NAMES[8] = {"food", "light", "game", "medicine", "bathroom", "meter", "discipline", "attention"};

size_t rm_look_text(const rm_look_t *l, char *out, size_t cap)
{
    size_t n = 0;
#define ROOM(k) ((k) < cap - n)  // keeps room for the NUL
    if (cap == 0) return 0;
    for (int y = 0; y < 16; y++) {
        if (!ROOM(33)) return 0;
        for (int x = 0; x < 32; x++) out[n++] = (l->frame.rows[y] >> x) & 1 ? '#' : '.';
        out[n++] = '\n';
    }
    char tail[160];
    int t = snprintf(tail, sizeof tail, "icons:");
    bool any = false;
    for (int i = 0; i < 8; i++)
        if (l->frame.icons & (1u << i)) {
            t += snprintf(tail + t, sizeof tail - t, " %s", RM_ICON_NAMES[i]);
            any = true;
        }
    if (!any) t += snprintf(tail + t, sizeof tail - t, " none");
    t += snprintf(tail + t, sizeof tail - t, "\nbuzzer: %s\n", l->buzzer ? "on" : "off");
    if (l->phase == RM_PHASE_CATCHUP)
        t += snprintf(tail + t, sizeof tail - t, "phase: catch-up %u%%\n", (unsigned)l->catchup_pct);
    else
        t += snprintf(tail + t, sizeof tail - t, "phase: %s\n", l->phase == RM_PHASE_BOOT ? "boot" : "run");
    t += snprintf(tail + t, sizeof tail - t, "frame: %lu\n", (unsigned long)l->seq);
    if (!ROOM((size_t)t + 1)) return 0;
    memcpy(out + n, tail, (size_t)t);
    n += (size_t)t;
    out[n] = 0;
    return n;
}

#define LOOK_TEXT_CAP 1024
#define LOOK_PNG_CAP 4096

bool rm_look_encode(const rm_look_t *l, char **text, char **png_b64)
{
    // Off the stack: callers run deep in the httpd task.
    char *t = rm_alloc(LOOK_TEXT_CAP);
    uint8_t *png = rm_alloc(LOOK_PNG_CAP);
    char *b64 = rm_alloc(4 * ((LOOK_PNG_CAP + 2) / 3) + 1);
    size_t tn = t ? rm_look_text(l, t, LOOK_TEXT_CAP) : 0;
    size_t pn = png ? rm_look_png(l, png, LOOK_PNG_CAP) : 0;
    bool ok = b64 && tn && pn;
    if (ok) rm_base64_encode(png, pn, b64);
    free(png);
    if (!ok) {
        free(t);
        free(b64);
        t = b64 = NULL;
    }
    *text = t;
    *png_b64 = b64;
    return ok;
}
