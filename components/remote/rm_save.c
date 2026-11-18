// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include "remote.h"

bool rm_restore_mode_parse(const char *s, rm_restore_mode_t *out)
{
    if (!s) return false;
    if (strcmp(s, "frozen") == 0) { *out = RM_RESTORE_FROZEN; return true; }
    if (strcmp(s, "replay") == 0) { *out = RM_RESTORE_REPLAY; return true; }
    return false;
}

bool rm_pet_id_parse(const char *s, uint8_t sha1[20], uint8_t *id)
{
    if (!s || strlen(s) != RM_PET_ID_LEN || s[40] != '-' || s[41] < '0' || s[41] > '9') return false;
    uint8_t h[20];
    for (int i = 0; i < 20; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[2 * i + k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v << 4 | (unsigned)d;
        }
        h[i] = (uint8_t)v;
    }
    if (!tc_model_playable(tc_model_from_sha1(h))) return false;
    memcpy(sha1, h, 20);
    *id = (uint8_t)(s[41] - '0');
    return true;
}

void rm_pet_id_format(const uint8_t sha1[20], uint8_t id, char out[RM_PET_ID_LEN + 1])
{
    for (int i = 0; i < 20; i++) snprintf(out + 2 * i, 3, "%02x", sha1[i]);
    snprintf(out + 40, 3, "-%u", id % 10u);
}

const char *rm_save_check(const uint8_t *rec, size_t len, const uint8_t sha1[20], uint8_t pet_id,
                          bool (*state_ok)(const uint8_t *payload, size_t len), ps_meta_t *meta_out)
{
    ps_meta_t m;
    const uint8_t *payload;
    size_t plen;
    if (!ps_record_decode(rec, len, &m, &payload, &plen)) return "record";
    if (meta_out) *meta_out = m;
    if (memcmp(m.rom_sha1, sha1, 20) != 0) return "rom";
    tc_model_t model = tc_model_from_sha1(m.rom_sha1);
    if (!tc_model_playable(model)) return "model";
    if (m.pet_id != pet_id) return "pet";
    if (state_ok && !state_ok(payload, plen)) return "state";
    return NULL;
}

void rm_save_filename(tc_model_t model, const ps_meta_t *meta, char out[48])
{
    char date[16] = "undated";
    if (meta->unix_time != 0) {
        // Civil date from days since 1970-01-01 (Hinnant), UTC, no locale or timezone.
        int64_t z = meta->unix_time / 86400;
        if (meta->unix_time % 86400 < 0) z--;
        z += 719468;
        int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        int64_t doe = z - era * 146097;
        int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        int64_t mp = (5 * doy + 2) / 153;
        int d = (int)(doy - (153 * mp + 2) / 5 + 1);
        int mo = (int)(mp < 10 ? mp + 3 : mp - 9);
        int64_t y = yoe + era * 400 + (mo <= 2);
        snprintf(date, sizeof date, "%04d-%02d-%02d", (int)y, mo, d);
    }
    const uint8_t *h = meta->rom_sha1;
    snprintf(out, 48, "tamaorb-%s-%02x%02x%02x%02x-%s.sav", tc_model_name(model), h[0], h[1], h[2], h[3], date);
}
