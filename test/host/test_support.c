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

const char *test_rom_name(const char *real)
{
    const char *t = getenv("TAMAORB_TEST_ROM");
    return t && *t ? t : real;
}

bool test_homebrew_rom(void)
{
    const char *t = getenv("TAMAORB_TEST_ROM");
    return t && *t;
}

bool test_require_rom(bool loaded)
{
    if (!loaded && test_homebrew_rom()) {
        fprintf(stderr, "TAMAORB_TEST_ROM=%s set but not loaded\n", getenv("TAMAORB_TEST_ROM"));
        exit(1);
    }
    return loaded;
}

void test_hex20(const char *hex, uint8_t out[20])
{
    for (int i = 0; i < 20; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}
