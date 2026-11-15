// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "hostlink.h"

static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int val(char c)
{
    const char *p = c ? strchr(ALPHABET, c) : NULL;
    return p ? (int)(p - ALPHABET) : -1;
}

int hl_b64_decode(const char *in, uint8_t *out, size_t cap)
{
    size_t n = strlen(in), o = 0;
    if (n % 4) return -1;
    for (size_t i = 0; i < n; i += 4) {
        int v[4];
        for (int k = 0; k < 4; k++) {
            char c = in[i + k];
            v[k] = (c == '=' && i + 4 == n && k >= 2) ? 0 : val(c);
            if (v[k] < 0) return -1;
        }
        uint32_t w = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 | (uint32_t)v[2] << 6 | (uint32_t)v[3];
        int bytes = in[i + 2] == '=' ? 1 : (in[i + 3] == '=' ? 2 : 3);
        if (o + (size_t)bytes > cap) return -1;
        for (int k = 0; k < bytes; k++) out[o++] = (uint8_t)(w >> (16 - 8 * k));
    }
    return (int)o;
}

size_t hl_b64_encode(const uint8_t *in, size_t len, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        if (o + 5 > cap) break;
        uint32_t w = (uint32_t)in[i] << 16 | (i + 1 < len ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < len ? in[i + 2] : 0);
        out[o++] = ALPHABET[(w >> 18) & 63];
        out[o++] = ALPHABET[(w >> 12) & 63];
        out[o++] = i + 1 < len ? ALPHABET[(w >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? ALPHABET[w & 63] : '=';
    }
    out[o] = '\0';
    return o;
}
