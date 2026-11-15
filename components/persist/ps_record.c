// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include "persist.h"

uint32_t ps_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put(uint8_t *p, uint64_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t get(const uint8_t *p, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

size_t ps_record_encode(const ps_meta_t *m, const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
    size_t total = PS_OVERHEAD + len;
    if (len > 0xFFFF || total > cap) return 0;
    put(out, PS_MAGIC, 4);
    out[4] = PS_VERSION;
    out[5] = m->pet_id;
    out[6] = m->time_trusted ? 1 : 0;
    out[7] = 0;
    put(out + 8, m->seq, 4);
    put(out + 12, (uint64_t)m->unix_time, 8);
    memcpy(out + 20, m->rom_sha1, 20);
    put(out + 40, len, 2);
    memcpy(out + PS_HEADER_BYTES, payload, len);
    put(out + PS_HEADER_BYTES + len, ps_crc32(out, PS_HEADER_BYTES + len), 4);
    return total;
}

bool ps_record_decode(const uint8_t *rec, size_t rec_len, ps_meta_t *m, const uint8_t **payload, size_t *len)
{
    if (rec_len < PS_OVERHEAD || get(rec, 4) != PS_MAGIC || rec[4] != PS_VERSION) return false;
    size_t plen = (size_t)get(rec + 40, 2);
    if (rec_len != PS_OVERHEAD + plen) return false;
    if (ps_crc32(rec, PS_HEADER_BYTES + plen) != (uint32_t)get(rec + PS_HEADER_BYTES + plen, 4)) return false;
    m->pet_id = rec[5];
    m->time_trusted = rec[6] & 1;
    m->seq = (uint32_t)get(rec + 8, 4);
    m->unix_time = (int64_t)get(rec + 12, 8);
    memcpy(m->rom_sha1, rec + 20, 20);
    *payload = rec + PS_HEADER_BYTES;
    *len = plen;
    return true;
}

void ps_key(const uint8_t sha1[20], uint8_t pet_id, char slot, char out[16])
{
    snprintf(out, 16, "s%02x%02x%02x%02x%02x_%u%c", sha1[0], sha1[1], sha1[2], sha1[3], sha1[4], pet_id % 10u, slot);
}
