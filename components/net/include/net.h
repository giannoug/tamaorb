// SPDX-License-Identifier: GPL-3.0-or-later
// Wi-Fi station, SNTP and "known time" (spec §6.4–6.5).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t net_start(void);                       // STA from NVS namespace "net" ("ssid", "pass"); no-op if unset
esp_err_t net_set_credentials(const char *ssid, const char *pass);
bool      net_wait_time(uint32_t timeout_ms);    // true once time is trusted (synced now or before a soft reset)
bool      net_wait_fresh_sync(uint32_t timeout_ms);  // waits for an NTP sync in this boot, whatever the trust
bool      net_now(int64_t *unix_s);              // returns whether the time is trusted
bool      net_connected(void);
bool      net_configured(void);
