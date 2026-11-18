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
    // One stored deflate block holds the whole raster, so it is drawn straight into its place in out:
    // no raster copy on the caller's (httpd) stack.
    _Static_assert(RAW_BYTES <= STORED_MAX, "the raster must fit one stored block");
    size_t zlen = 2 + 5 + RAW_BYTES + 4;
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

    // IDAT: length, type, zlib header, the stored block's header, the raster, Adler-32, CRC.
    uint8_t *z = o + 8;
    z[0] = 0x78;
    z[1] = 0x01;
    z[2] = 1;  // final block, stored
    z[3] = RAW_BYTES & 0xFF; z[4] = RAW_BYTES >> 8;
    uint16_t nlen = (uint16_t)~(uint16_t)RAW_BYTES;
    z[5] = nlen & 0xFF; z[6] = nlen >> 8;
    uint8_t *raw = z + 7;
    memset(raw, 0xFF, RAW_BYTES);
    for (int y = 0; y < RM_PNG_H; y++) raw[y * (ROW_BYTES + 1)] = 0;  // filter: none
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if ((l->frame.rows[y] >> x) & 1)
                for (int dy = 0; dy < 4; dy++)
                    for (int dx = 0; dx < 4; dx++) ink(raw, x * 4 + dx, 16 + y * 4 + dy);
    for (int i = 0; i < 8; i++) {
        if (!(l->frame.icons & (1u << i))) continue;
        const char *const *art = ui_icon_art(l->model, i);
        draw_icon(raw, art, 10 + 32 * (i % 4), i < 4 ? 0 : 84);
    }
    uint32_t a = 1, b = 0;
    for (size_t k = 0; k < RAW_BYTES; k++) { a = (a + raw[k]) % 65521; b = (b + a) % 65521; }
    put32(raw + RAW_BYTES, b << 16 | a);
    put32(o, (uint32_t)zlen);
    memcpy(o + 4, "IDAT", 4);
    put32(o + 8 + zlen, ps_crc32(o + 4, 4 + zlen));
    o += 12 + zlen;
    o = chunk(o, "IEND", NULL, 0);
    return (size_t)(o - out);
}
