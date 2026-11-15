// SPDX-License-Identifier: GPL-3.0-or-later
// Saves in the "tamaorb" NVS partition, two alternating keys per pet (spec §6.2–6.3).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "persist.h"

// Loads the newest valid record for this ROM into tama_core (call after tc_init, before the
// emulator task runs). ESP_ERR_NOT_FOUND when there is none.
esp_err_t saver_load(const uint8_t sha1[20], ps_meta_t *meta);
void      saver_request(void);                                  // any task; saved at the emulator's next loop
bool      saver_request_and_wait(uint32_t timeout_ms);          // any task except the emulator's; true if saved
void      saver_set_time_source(bool (*now)(int64_t *unix_s));  // returns trusted; default: untrusted 0
// Emulator task only: applies the save policy and pending requests.
void      saver_poll(uint32_t now_ms);
// Emulator task only: saves immediately with an explicit timestamp (catch-up progress saves).
esp_err_t saver_save_at(int64_t unix_s, bool trusted);
// Emulator task during catch-up: serve a pending request with the catch-up's virtual time.
void      saver_serve_request_at(int64_t unix_s, bool trusted);
