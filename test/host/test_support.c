// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_support.h"
#include <stdio.h>
#include <stdlib.h>

bool test_load_file(const char *name, uint8_t *buf, size_t size)
{
    const char *dir = getenv("TAMAORB_ROM_DIR");
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir ? dir : TAMAORB_ROM_DIR_DEFAULT, name);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, size, f);
    int extra = fgetc(f);
    fclose(f);
    return n == size && extra == EOF;
}

void test_hex20(const char *hex, uint8_t out[20])
{
    for (int i = 0; i < 20; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}
