// SPDX-License-Identifier: GPL-3.0-or-later
// Delayed restarts, and switching, creating and deleting pets: shared by the on-device screens and the remote API.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pet_store.h"

// Any task: esp_restart() after ms (esp_timer one-shot), so a reply can go out first. Later calls are ignored.
void      app_restart_after(uint32_t ms);
bool      app_restart_pending(void);

// The pet a ROM starts with when none is chosen: its pet with the newest save, else its lowest pet, else a new
// pet 0 (created). Boot and USB pushes use it (spec: "a ROM becomes active with no pet chosen").
esp_err_t pet_for_rom(const uint8_t sha1[20], tc_model_t m, pet_ref_t *out);

// Boot, once the running pet is known and whether the question is asked: reads the "previous pet" record (Back
// after a switch). It stays while the question waits, so a restart asks again with Back; it is erased when the
// boot doesn't ask, when it names the running pet, or when its ROM is not on the card.
void      pet_switch_boot(bool asking);
// The question was answered: Back is gone (the record is erased).
void      pet_prev_forget(void);
// This boot's Back target, with its name, while the question waits; false once its pet is deleted.
bool      pet_prev(pet_ref_t *out, char name[PS_NAME_MAX + 1]);

// Any task, EMU_RUN only. Each draws nothing; on success the ball restarts in 1 s.
// Saves the running pet (<= 3 s), makes p's ROM the active ROM, records prev = running and active = p.
// ESP_ERR_INVALID_STATE: wrong phase. ESP_ERR_NOT_ALLOWED: a switch or restart is under way. ESP_ERR_NOT_FOUND: no
// such pet, or its ROM is not on the SD card. ESP_FAIL: the save failed (or saving is frozen), nothing changed.
esp_err_t pet_switch_to(const pet_ref_t *p);
// A new pet of a ROM on the card, then as pet_switch_to. ESP_ERR_NO_MEM: no free pet number or NVS is full.
esp_err_t pet_new(const uint8_t sha1[20], pet_ref_t *created);
// Not the running pet (ESP_ERR_INVALID_STATE); no restart. ESP_ERR_NOT_FOUND: no such pet.
esp_err_t pet_delete(const pet_ref_t *p);
// EMU_WAIT only: back to this boot's previous pet (pet_prev), leaving the waiting pet untouched (no save, no clock).
// ESP_ERR_INVALID_STATE: not waiting. ESP_ERR_NOT_FOUND: no previous pet, or its ROM is not on the card.
// ESP_ERR_NOT_ALLOWED: a restart is under way. On success the ball restarts in 1 s.
esp_err_t pet_switch_back(void);
#ifdef TC_TESTING
void      rom_switch_test_reset(void);  // host tests: no restart pending, no switch under way, no Back
#endif
