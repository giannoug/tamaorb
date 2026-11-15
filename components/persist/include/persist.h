// SPDX-License-Identifier: GPL-3.0-or-later
// Save records and save timing (spec §6.1–6.3). Portable: no ESP-IDF headers.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS_MAGIC   0x42524F54u  // "TORB" little-endian
#define PS_VERSION 1
#define PS_HEADER_BYTES 42      // magic..payload_len
#define PS_OVERHEAD (PS_HEADER_BYTES + 4)

typedef struct {
    uint8_t  rom_sha1[20];
    uint8_t  pet_id;        // always 0 in v1
    bool     time_trusted;
    int64_t  unix_time;     // wall clock when saved (virtual time for catch-up progress saves)
    uint32_t seq;           // increases with every save
} ps_meta_t;

// Layout (little-endian): magic u32, version u8, pet_id u8, flags u8 (bit0 time_trusted), reserved u8,
// seq u32, unix_time i64, rom_sha1[20], payload_len u16, payload, crc32 u32 (IEEE, over everything before it).
size_t ps_record_encode(const ps_meta_t *m, const uint8_t *payload, size_t len, uint8_t *out, size_t cap);
bool   ps_record_decode(const uint8_t *rec, size_t rec_len, ps_meta_t *m, const uint8_t **payload, size_t *len);
// NVS key, max 15 chars: "s" + first 10 hex digits of sha1 + "_" + pet digit + slot ('a' or 'b').
void   ps_key(const uint8_t sha1[20], uint8_t pet_id, char slot, char out[16]);
uint32_t ps_crc32(const uint8_t *data, size_t len);
// Of the two A/B slots, which holds the newest valid record: 0 = a, 1 = b, -1 = neither.
// Sequence numbers compare wrap-safely.
int    ps_pick_newest(bool a_ok, const ps_meta_t *a, bool b_ok, const ps_meta_t *b);

#define PS_SAVE_AFTER_RELEASE_MS 3000u
#define PS_SAVE_PERIOD_MS        300000u

#define PS_RETRY_BACKOFF_MS      30000u

typedef struct { uint32_t last_save_ms, last_release_ms, retry_after_ms; bool dirty_input, backing_off; } ps_policy_t;
void ps_policy_init(ps_policy_t *p, uint32_t now_ms);
void ps_policy_on_buttons(ps_policy_t *p, uint32_t now_ms, uint8_t prev_mask, uint8_t mask);
bool ps_policy_due(const ps_policy_t *p, uint32_t now_ms);
void ps_policy_saved(ps_policy_t *p, uint32_t now_ms);
void ps_policy_failed(ps_policy_t *p, uint32_t now_ms);  // no policy save is due for PS_RETRY_BACKOFF_MS
