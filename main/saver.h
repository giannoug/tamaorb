// SPDX-License-Identifier: GPL-3.0-or-later
// Saves in the "tamaorb" NVS partition, two alternating keys per pet.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "persist.h"

// Loads the newest valid record for this ROM into tama_core (call after tc_init, before the
// emulator task runs). ESP_ERR_NOT_FOUND when there is none.
esp_err_t saver_load(const uint8_t sha1[20], ps_meta_t *meta);
void      saver_request(void);                                  // any task; saved at the emulator's next loop
bool      saver_request_and_wait(uint32_t timeout_ms);          // any task except the emulator's; true if saved
                                                                // (false at once after an import froze saving)
void      saver_set_time_source(bool (*now)(int64_t *unix_s));  // returns trusted; default: untrusted 0
// Emulator task only: applies the save policy and pending requests.
void      saver_poll(uint32_t now_ms);
// Emulator task only: saves immediately with an explicit timestamp (catch-up progress saves).
esp_err_t saver_save_at(int64_t unix_s, bool trusted);
// Emulator task during catch-up: serve a pending request with the catch-up's virtual time.
void      saver_serve_request_at(int64_t unix_s, bool trusted);

// Any task: the newest valid record of any pet (by its ROM's SHA-1), as stored. rec may be NULL (meta only);
// otherwise ESP_ERR_INVALID_SIZE if cap is too small. ESP_ERR_NOT_FOUND when that pet has no valid record.
esp_err_t saver_read_newest(const uint8_t sha1[20], ps_meta_t *meta, uint8_t *rec, size_t cap, size_t *len);
// Any task: writes rec (already restamped by the caller with seq = newest + 1) into that pet's older slot,
// so the newest existing record survives a power cut mid-write. ESP_ERR_INVALID_STATE if the newest seq
// moved on meanwhile (re-read, restamp, retry). If sha1 is the running pet, saving stops for the rest of
// this boot once this succeeds (the caller restarts); on failure nothing was written and saving goes on.
esp_err_t saver_import(const uint8_t sha1[20], const uint8_t *rec, size_t len);
