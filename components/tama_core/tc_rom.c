// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "tc_rom.h"

// SHA-1 values from MAME src/mame/handheld/hh_e0c6200.cpp.
static const struct { uint8_t sha1[20]; tc_model_t model; } KNOWN[] = {
    {{0x4b,0x49,0x79,0xcf,0x92,0xdc,0x9d,0x2f,0xb6,0xd7,0x29,0x5a,0x38,0xf2,0x09,0xf3,0xda,0x14,0x4f,0x72}, TC_MODEL_P1},
    {{0x09,0xe5,0x10,0x1b,0x37,0x63,0x6a,0x31,0x4f,0xc5,0x99,0xd5,0xd6,0x9b,0x48,0x46,0x72,0x1b,0x3c,0x88}, TC_MODEL_P2},
    {{0x6e,0x13,0xd0,0x15,0x11,0x3e,0x16,0x19,0x8c,0x00,0x59,0xb9,0xd0,0xc3,0x8d,0x70,0x27,0xae,0x73,0x24}, TC_MODEL_TEST_ROM},
    {{0xf5,0x89,0x9b,0xb7,0x71,0x77,0x56,0xac,0x58,0x14,0x51,0xcf,0x16,0xcf,0x97,0xd9,0x09,0x96,0x1c,0x5c}, TC_MODEL_ANGEL},
    {{0x74,0xc1,0xf6,0x76,0x17,0x24,0xb7,0xcb,0xda,0x8b,0xca,0x31,0x13,0xdb,0x78,0x58,0x6b,0x78,0x6d,0x2d}, TC_MODEL_MOTHRA},
};

bool tc_rom_size_ok(size_t len) { return len == TC_ROM_BYTES_S46 || len == TC_ROM_BYTES_S48; }

bool tc_model_playable(tc_model_t m)
{
    return m == TC_MODEL_P1 || m == TC_MODEL_P2 || m == TC_MODEL_ANGEL || m == TC_MODEL_MOTHRA;
}

size_t tc_model_rom_bytes(tc_model_t m)
{
    switch (m) {
    case TC_MODEL_P1: case TC_MODEL_P2: case TC_MODEL_TEST_ROM: return TC_ROM_BYTES_S46;
    case TC_MODEL_ANGEL: case TC_MODEL_MOTHRA: return TC_ROM_BYTES_S48;
    default: return 0;
    }
}

tc_model_t tc_model_from_sha1(const uint8_t sha1[20])
{
    for (size_t i = 0; i < sizeof KNOWN / sizeof KNOWN[0]; i++)
        if (memcmp(sha1, KNOWN[i].sha1, 20) == 0) return KNOWN[i].model;
    return TC_MODEL_UNKNOWN;
}

tc_model_t tc_rom_identify(const uint8_t *data, size_t len, uint8_t sha1_out[20])
{
    uint8_t h[20];
    tc_sha1(data, len, h);
    if (sha1_out) memcpy(sha1_out, h, 20);
    tc_model_t m = tc_model_from_sha1(h);
    return len == tc_model_rom_bytes(m) ? m : TC_MODEL_UNKNOWN;
}

bool tc_rom_decode(const uint8_t *data, size_t len, uint16_t *words_out)
{
    if (!tc_rom_size_ok(len)) return false;
    for (size_t i = 0; i < len / 2; i++)
        words_out[i] = (uint16_t)(((data[2 * i] << 8) | data[2 * i + 1]) & 0x0FFF);
    return true;
}

const char *tc_model_name(tc_model_t m)
{
    switch (m) {
    case TC_MODEL_P1: return "P1";
    case TC_MODEL_P2: return "P2";
    case TC_MODEL_TEST_ROM: return "test ROM";
    case TC_MODEL_ANGEL: return "Angel";
    case TC_MODEL_MOTHRA: return "Mothra";
    default: return "unknown";
    }
}

const char *tc_model_long_name(tc_model_t m)
{
    switch (m) {
    case TC_MODEL_P1: return "Gen 1";
    case TC_MODEL_P2: return "Gen 2";
    case TC_MODEL_ANGEL: return "Angel";
    case TC_MODEL_MOTHRA: return "Mothra";
    default: return NULL;
    }
}
