// SPDX-License-Identifier: GPL-3.0-or-later
// Pets: a ROM (SHA-1) plus a pet number 0-9. Their names and the active and previous pet live in the "pets"
// namespace of the "tamaorb" NVS partition; their saves are the saver's. A pet exists if it has a name entry or
// a save, so saves written before pets had names show up as pets with an automatic name.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "persist.h"
#include "tc_rom.h"

typedef struct { uint8_t sha1[20]; uint8_t id; } pet_ref_t;
typedef struct {
    pet_ref_t ref;
    char name[PS_NAME_MAX + 1];
    tc_model_t model;
    bool has_save, save_trusted;
    int64_t save_unix;
    uint32_t seq;
    int16_t age_years;  // -1 unknown
    bool egg;           // an egg in the saved RAM, or no save yet
    bool dead;          // dead in the saved RAM (tc_pet_t.dead)
    bool rom_on_card;
} pet_info_t;

#define PET_STORE_MAX 40  // a list never holds more (4 models x 10)

esp_err_t pet_store_init(void);                      // after rom_store_init; no-op when NVS is unusable
// Every pet, sorted by model then number. Fills names it lacks (an automatic one, stored). Core 0; reads NVS
// and the SD card, so it takes a moment.
int       pet_store_list(pet_info_t *out, int max);
bool      pet_store_active(pet_ref_t *out);          // false: none recorded
esp_err_t pet_store_set_active(const pet_ref_t *p);
bool      pet_store_prev(pet_ref_t *out);            // the "prev" record (Back after a switch); false: none
esp_err_t pet_store_set_prev(const pet_ref_t *p);
void      pet_store_clear_prev(void);
// The stored name, else an automatic one (stored). ESP_ERR_NOT_FOUND: no such pet (it is never created here).
// A few answers are cached (status polls ask every few seconds); creating, renaming and deleting clear them.
esp_err_t pet_store_name(const pet_ref_t *p, char out[PS_NAME_MAX + 1]);
// A new pet of that ROM (lowest free number, automatic name). ESP_ERR_NO_MEM: all ten used, or NVS is full.
esp_err_t pet_store_create(const uint8_t sha1[20], tc_model_t m, pet_ref_t *out);
// ESP_ERR_INVALID_ARG: not a valid name; ESP_ERR_INVALID_STATE: another pet has it; ESP_ERR_NOT_FOUND: no such pet.
// Writes nothing unless it succeeds.
esp_err_t pet_store_rename(const pet_ref_t *p, const char *name);
// Erases the saves, then the name. ESP_ERR_INVALID_STATE for the running pet; ESP_ERR_NOT_FOUND: no such pet.
esp_err_t pet_store_delete(const pet_ref_t *p);
bool      pet_ref_eq(const pet_ref_t *a, const pet_ref_t *b);
#ifdef TC_TESTING
void      pet_store_test_reset(void);  // host tests: forget cached names between tests
#endif
