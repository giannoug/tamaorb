// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rom_path(const char *name, char *path, size_t cap)
{
    const char *dir = getenv("TAMAORB_ROM_DIR");
    snprintf(path, cap, "%s/%s", dir ? dir : TAMAORB_ROM_DIR_DEFAULT, name);
}

bool test_load_file(const char *name, uint8_t *buf, size_t size)
{
    char path[512];
    rom_path(name, path, sizeof path);
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
    if (t && *t) return t;
    char path[512];
    rom_path(real, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return real; }
    return "testclock.bin";
}

bool test_homebrew_rom(void) { return strcmp(test_rom_name("tama.bin"), "tama.bin") != 0; }

bool test_require_rom(bool loaded)
{
    const char *t = getenv("TAMAORB_TEST_ROM");
    if (!loaded && t && *t) {
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
