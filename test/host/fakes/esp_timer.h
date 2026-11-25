// SPDX-License-Identifier: GPL-3.0-or-later
// Host fake of esp_timer.h: a one-shot timer fires at once when started.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct fake_timer *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *arg);
    void *arg;
    int dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out);
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us);
int64_t   esp_timer_get_time(void);
