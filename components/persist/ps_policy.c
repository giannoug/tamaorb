// SPDX-License-Identifier: GPL-3.0-or-later
// When to save: a few seconds after the last button release, and periodically.
#include "persist.h"

void ps_policy_init(ps_policy_t *p, uint32_t now_ms)
{
    *p = (ps_policy_t){ .last_save_ms = now_ms };
}

void ps_policy_on_buttons(ps_policy_t *p, uint32_t now_ms, uint8_t prev_mask, uint8_t mask)
{
    if (prev_mask & ~mask) {  // at least one button released
        p->dirty_input = true;
        p->last_release_ms = now_ms;
    }
}

bool ps_policy_due(const ps_policy_t *p, uint32_t now_ms)
{
    if (p->backing_off && (int32_t)(now_ms - p->retry_after_ms) < 0) return false;
    if (p->dirty_input && now_ms - p->last_release_ms >= PS_SAVE_AFTER_RELEASE_MS) return true;
    return now_ms - p->last_save_ms >= PS_SAVE_PERIOD_MS;
}

void ps_policy_saved(ps_policy_t *p, uint32_t now_ms)
{
    p->last_save_ms = now_ms;
    p->dirty_input = false;
    p->backing_off = false;
}

void ps_policy_failed(ps_policy_t *p, uint32_t now_ms)
{
    p->backing_off = true;
    p->retry_after_ms = now_ms + PS_RETRY_BACKOFF_MS;
}
