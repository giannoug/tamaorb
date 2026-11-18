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

const char *rm_save_check(const uint8_t *rec, size_t len, const uint8_t sha1[20],
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
    if (m.pet_id != 0) return "pet";
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
