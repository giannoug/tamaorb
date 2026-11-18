// SPDX-License-Identifier: GPL-3.0-or-later
// Pet rules that need no storage: save keys back to pets, names, free slots, the pet a ROM starts with.
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "persist.h"

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool ps_key_parse(const char *key, uint8_t sha_prefix[5], uint8_t *pet_id, char *slot)
{
    // "s" + 10 lower-case hex digits + "_" + pet digit + slot ('a' or 'b'): exactly what ps_key writes.
    if (!key || strlen(key) != 14 || key[0] != 's' || key[11] != '_') return false;
    uint8_t pre[5];
    for (int i = 0; i < 5; i++) {
        int hi = hexval(key[1 + 2 * i]), lo = hexval(key[2 + 2 * i]);
        if (hi < 0 || lo < 0) return false;
        pre[i] = (uint8_t)(hi << 4 | lo);
    }
    if (key[12] < '0' || key[12] > '9' || (key[13] != 'a' && key[13] != 'b')) return false;
    memcpy(sha_prefix, pre, 5);
    *pet_id = (uint8_t)(key[12] - '0');
    *slot = key[13];
    return true;
}

bool ps_name_valid(const char *name)
{
    if (!name) return false;
    size_t n = strlen(name);
    if (n < 1 || n > PS_NAME_MAX || name[0] == ' ' || name[n - 1] == ' ') return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7E) return false;
    return true;
}

bool ps_name_taken(const char *name, const char *const *names, int n)
{
    for (int i = 0; i < n; i++)
        if (names[i] && strcasecmp(name, names[i]) == 0) return true;
    return false;
}

void ps_name_auto(const char *model_name, const char *const *names, int n, char out[PS_NAME_MAX + 1])
{
    for (int k = 1;; k++) {
        snprintf(out, PS_NAME_MAX + 1, "%s #%d", model_name, k);
        if (!ps_name_taken(out, names, n)) return;
    }
}

int ps_free_pet_id(const bool used[PS_PETS_PER_ROM])
{
    for (int i = 0; i < PS_PETS_PER_ROM; i++)
        if (!used[i]) return i;
    return -1;
}

// a newer than b: trusted times first (by time, then seq); untrusted ones only by seq.
static bool newer(const ps_pet_seen_t *a, const ps_pet_seen_t *b)
{
    if (a->save_trusted != b->save_trusted) return a->save_trusted;
    if (a->save_trusted && a->save_unix != b->save_unix) return a->save_unix > b->save_unix;
    return a->seq > b->seq;
}

int ps_pick_for_rom(const ps_pet_seen_t pets[PS_PETS_PER_ROM])
{
    int best = -1, first = -1;
    for (int i = 0; i < PS_PETS_PER_ROM; i++) {
        if (!pets[i].exists) continue;
        if (first < 0) first = i;
        if (pets[i].has_save && (best < 0 || newer(&pets[i], &pets[best]))) best = i;
    }
    return best >= 0 ? best : first;
}
