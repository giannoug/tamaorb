// SPDX-License-Identifier: GPL-3.0-or-later
// Screen timeout and wake rules (spec §5.3–5.4). Pure: the caller supplies the time.
#include "ui.h"

#define BOOT_LONG_MS 2000u

void ui_screen_init(ui_screen_t *s, uint32_t now_ms)
{
    *s = (ui_screen_t){ .on = true, .last_activity_ms = now_ms };
}

static void wake(ui_screen_t *s, uint32_t now_ms)
{
    s->on = true;
    s->last_activity_ms = now_ms;
}

ui_screen_out_t ui_screen_update(ui_screen_t *s, uint32_t now_ms, uint8_t touch_buttons,
                                 bool boot_down, bool sound_on, uint32_t timeout_ms)
{
    if (sound_on) {
        if (!s->sound_was_on) wake(s, now_ms);  // a beep turns the screen on, without swallowing
        s->last_activity_ms = now_ms;
    }
    s->sound_was_on = sound_on;

    if (touch_buttons) {
        if (!s->on) s->swallowing = true;  // first touch on a dark screen only wakes it
        wake(s, now_ms);
    } else {
        s->swallowing = false;
    }

    if (boot_down && !s->boot_was_down) {
        s->boot_down_ms = now_ms;
        s->long_fired = false;
    }
    if (boot_down && !s->long_fired && now_ms - s->boot_down_ms >= BOOT_LONG_MS) {
        s->long_fired = s->long_pending = true;  // reported while still held
        wake(s, now_ms);
    }
    if (!boot_down && s->boot_was_down && !s->long_fired) {
        if (s->on) s->on = false;
        else wake(s, now_ms);
    }
    s->boot_was_down = boot_down;

    if (timeout_ms && s->on && now_ms - s->last_activity_ms >= timeout_ms) s->on = false;

    return (ui_screen_out_t){ .buttons = (s->on && !s->swallowing) ? touch_buttons : 0, .backlight = s->on };
}

bool ui_screen_long_press(ui_screen_t *s)
{
    bool p = s->long_pending;
    s->long_pending = false;
    return p;
}

void ui_screen_resume(ui_screen_t *s, uint32_t now_ms, bool boot_down, bool touch_down)
{
    ui_screen_init(s, now_ms);
    s->boot_was_down = boot_down;
    s->long_fired = boot_down;   // a held press finishes without toggling the screen
    s->swallowing = touch_down;  // a held touch is not forwarded until released
}

void ui_screen_wake(ui_screen_t *s, uint32_t now_ms) { wake(s, now_ms); }

bool ui_screen_dimmed(const ui_screen_t *s, uint32_t now_ms, uint32_t dim_ms)
{
    return s->on && now_ms - s->last_activity_ms >= dim_ms;
}
