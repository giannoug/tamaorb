// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "remote.h"
#include "ui_internal.h"
#include "persist.h"

#define ROW_BYTES (RM_PNG_W / 8)
#define RAW_BYTES (RM_PNG_H * (ROW_BYTES + 1))
#define STORED_MAX 65535

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void ink(uint8_t *raw, int x, int y)
{
    raw[y * (ROW_BYTES + 1) + 1 + x / 8] &= (uint8_t)~(0x80u >> (x % 8));
}

static void draw_icon(uint8_t *raw, const char *const rows[12], int x0, int y0)
{
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++)
            if (rows[y][x] == '#') ink(raw, x0 + x, y0 + y);
}

static uint8_t *chunk(uint8_t *o, const char *type, const uint8_t *data, size_t len)
{
    put32(o, (uint32_t)len);
    memcpy(o + 4, type, 4);
    if (len) memcpy(o + 8, data, len);
    put32(o + 8 + len, ps_crc32(o + 4, 4 + len));
    return o + 12 + len;
}

size_t rm_look_png(const rm_look_t *l, uint8_t *out, size_t cap)
{
    uint8_t raw[RAW_BYTES];
    memset(raw, 0xFF, sizeof raw);
    for (int y = 0; y < RM_PNG_H; y++) raw[y * (ROW_BYTES + 1)] = 0;  // filter: none
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if ((l->frame.rows[y] >> x) & 1)
                for (int dy = 0; dy < 4; dy++)
                    for (int dx = 0; dx < 4; dx++) ink(raw, x * 4 + dx, 16 + y * 4 + dy);
    for (int i = 0; i < 8; i++) {
        if (!(l->frame.icons & (1u << i))) continue;
        const char *const *art = (i == 3 && l->model == TC_MODEL_P2) ? UI_ICON_P2_MEDICINE : UI_ICONS[i];
        draw_icon(raw, art, 10 + 32 * (i % 4), i < 4 ? 0 : 84);
    }

    size_t blocks = (RAW_BYTES + STORED_MAX - 1) / STORED_MAX;
    size_t zlen = 2 + RAW_BYTES + 5 * blocks + 4;
    size_t total = 8 + (12 + 13) + (12 + zlen) + 12;
    if (cap < total) return 0;

    uint8_t *o = out;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    memcpy(o, sig, 8);
    o += 8;
    uint8_t ihdr[13];
    put32(ihdr, RM_PNG_W);
    put32(ihdr + 4, RM_PNG_H);
    ihdr[8] = 1; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    o = chunk(o, "IHDR", ihdr, sizeof ihdr);

    // IDAT: build the zlib stream in place after the chunk header.
    uint8_t *z = o + 8;
    size_t zi = 0;
    z[zi++] = 0x78;
    z[zi++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t k = 0; k < RAW_BYTES; k++) { a = (a + raw[k]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < RAW_BYTES; off += STORED_MAX) {
        size_t n = RAW_BYTES - off < STORED_MAX ? RAW_BYTES - off : STORED_MAX;
        z[zi++] = (off + n == RAW_BYTES) ? 1 : 0;
        z[zi++] = n & 0xFF; z[zi++] = n >> 8;
        z[zi++] = ~n & 0xFF; z[zi++] = (~n >> 8) & 0xFF;
        memcpy(z + zi, raw + off, n);
        zi += n;
    }
    put32(z + zi, b << 16 | a);
    zi += 4;
    put32(o, (uint32_t)zi);
    memcpy(o + 4, "IDAT", 4);
    put32(o + 8 + zi, ps_crc32(o + 4, 4 + zi));
    o += 12 + zi;
    o = chunk(o, "IEND", NULL, 0);
    return (size_t)(o - out);
}
