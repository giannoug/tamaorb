// SPDX-License-Identifier: GPL-3.0-or-later
// Wi-Fi station, SNTP and "known time".
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// STA from NVS namespace "net" ("ssid", "pass"); no-op if unset. hostname goes to DHCP; ntp_server is copied.
esp_err_t net_start(const char *hostname, const char *ntp_server);
esp_err_t net_set_credentials(const char *ssid, const char *pass);
bool      net_wait_time(uint32_t timeout_ms);    // true once time is trusted (synced now or before a soft reset)
bool      net_wait_fresh_sync(uint32_t timeout_ms);  // waits for an NTP sync in this boot, whatever the trust
bool      net_now(int64_t *unix_s);              // returns whether the time is trusted
void      net_forget_time(void);                 // factory reset: the time is not trusted until the next sync
bool      net_connected(void);
bool      net_configured(void);
bool      net_ip(char out[16]);                  // false (and "") until an address is assigned
int8_t    net_rssi(void);                        // 0 if not connected
// Wi-Fi power save off (true: lowest latency, e.g. while a debugger is open) or back to the default (false).
void net_set_low_latency(bool on);
