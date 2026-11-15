// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "remote.h"

void rm_press_defaults(rm_press_req_t *r)
{
    memset(r, 0, sizeof *r);
    r->hold_ms = 150;
    r->gap_ms = 200;
    r->settle_ms = 500;
}

bool rm_press_add(rm_press_req_t *r, const char *token)
{
    if (r->n >= RM_PRESS_MAX_STEPS || !token || !*token) return false;
    unsigned m = 0;
    for (const char *c = token; *c; c++) {
        switch (*c) {
        case 'A': case 'a': m |= 1; break;
        case 'B': case 'b': m |= 2; break;
        case 'C': case 'c': m |= 4; break;
        default: return false;
        }
    }
    if (m != 1 && m != 2 && m != 4 && m != 5) return false;
    r->steps[r->n++] = (rm_step_t)m;
    return true;
}

bool rm_press_parse(rm_press_req_t *r, const char *spec)
{
    if (!spec || !*spec) return false;
    char tok[4];
    const char *p = spec;
    for (;;) {
        const char *e = strchr(p, ' ');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 0 || len >= sizeof tok) return false;
        memcpy(tok, p, len);
        tok[len] = 0;
        if (!rm_press_add(r, tok)) return false;
        if (!e) return true;
        p = e + 1;
    }
}

static uint32_t total_ms(const rm_press_req_t *r)
{
    return (uint32_t)r->n * r->hold_ms + (uint32_t)(r->n - 1) * r->gap_ms + r->settle_ms;
}

const char *rm_press_validate(const rm_press_req_t *r)
{
    if (r->n < 1 || r->n > RM_PRESS_MAX_STEPS) return "steps";
    if (r->hold_ms < 50 || r->hold_ms > 2000) return "hold_ms";
    if (r->gap_ms > 2000) return "gap_ms";
    if (r->settle_ms > 3000) return "settle_ms";
    if (total_ms(r) > 10000) return "too long";
    return NULL;
}

void rm_press_begin(rm_press_t *p, const rm_press_req_t *r, uint32_t now_ms)
{
    p->req = *r;
    p->start_ms = now_ms;
    p->active = r->n > 0;
}

uint8_t rm_press_mask(const rm_press_t *p, uint32_t now_ms)
{
    if (!p->active) return 0;
    uint32_t el = now_ms - p->start_ms;
    uint32_t period = (uint32_t)p->req.hold_ms + p->req.gap_ms;
    uint32_t k = el / period;
    if (k >= (uint32_t)p->req.n || el % period >= p->req.hold_ms) return 0;
    return (uint8_t)p->req.steps[k];
}

bool rm_press_done(const rm_press_t *p, uint32_t now_ms)
{
    return now_ms - p->start_ms >= total_ms(&p->req);
}

bool rm_press_window(const rm_press_t *p, uint32_t now_ms)
{
    return p->active && !rm_press_done(p, now_ms);
}
