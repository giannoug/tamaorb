// SPDX-License-Identifier: GPL-3.0-or-later
// Catch-up speed on the PC, with and without halt fast-forward (not a test; a measuring tool):
//   build-host/catchup_bench <rom.bin> <hours>
// Starts as test_halt_ff.c does: P1/P2 from the owner's saves, Angel/Mothra from a fresh egg out of its clock
// setup. Prints emulated seconds per wall second for each mode. The ball is slower; its speeds come from the
// ball's own "caught up" log lines (docs/architecture.md).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "persist.h"
#include "remote.h"
#include "tama_core.h"
#include "tc_pet.h"
#include "tc_rom.h"
#include "tc_time.h"
#include "test_support.h"

static uint64_t now_us(void) { return 0; }
static void sleep_until_us(uint64_t ts) { (void)ts; }

static void run_ms(uint32_t ms)
{
    uint32_t start = tc_tick_counter(), want = (uint32_t)((uint64_t)ms * TC_TICK_HZ / 1000);
    while (tc_ticks_since(start, tc_tick_counter()) < want) tc_step(1);
}

static void play(const char *spec)
{
    rm_press_req_t r;
    rm_press_defaults(&r);
    r.gap_ms = TC_PET_SETUP_GAP_MS;
    if (!rm_press_parse(&r, spec)) return;
    for (int i = 0; i < r.n; i++) {
        tc_post_buttons((uint8_t)r.steps[i]);
        run_ms(r.hold_ms);
        tc_post_buttons(0);
        run_ms(r.gap_ms);
    }
}

static bool load_save(const char *file)
{
    char path[512];
    snprintf(path, sizeof path, "%s/roms/saves/%s", TAMAORB_ROOT_DEFAULT, file);
    static uint8_t rec[4096];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t len = fread(rec, 1, sizeof rec, f);
    fclose(f);
    ps_meta_t m;
    const uint8_t *payload;
    size_t plen;
    return ps_record_decode(rec, len, &m, &payload, &plen) && tc_state_load(payload, plen);
}

static double wall_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <rom.bin> <hours>\n", argv[0]);
        return 2;
    }
    static uint8_t rom[TC_ROM_MAX_BYTES];
    static uint16_t program[TC_ROM_MAX_WORDS];
    static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us };
    size_t len = 0;
    for (size_t sz = TC_ROM_BYTES_S46; !len && sz <= TC_ROM_BYTES_S48; sz += TC_ROM_BYTES_S48 - TC_ROM_BYTES_S46)
        if (test_load_file(argv[1], rom, sz)) len = sz;
    tc_model_t model = len ? tc_rom_identify(rom, len, NULL) : TC_MODEL_UNKNOWN;
    if (!tc_model_playable(model) || !tc_rom_decode(rom, len, program) || !tc_init(program, &plat)) {
        fprintf(stderr, "no playable dump: %s\n", argv[1]);
        return 1;
    }
    tc_set_realtime(false);
    tc_set_icon_pins(tc_model_icon_pins(model));
    tc_set_clock_addr((uint16_t)tc_pet_clock_addr(model));
    const char *save = model == TC_MODEL_P1 ? "p1.sav" : model == TC_MODEL_P2 ? "p2.sav" : NULL;
    if (save && !load_save(save)) {
        fprintf(stderr, "no save: roms/saves/%s\n", save);
        return 1;
    }
    if (!save) {
        run_ms(3000);
        play(tc_pet_setup_exit(model));
        run_ms(60000);
    }
    size_t n = tc_state_size();
    uint8_t *start = malloc(n);
    tc_state_save(start, n);
    double hours = atof(argv[2]);
    uint64_t ticks = (uint64_t)(hours * 3600.0 * TC_TICK_HZ);
    for (int ff = 0; ff < 2; ff++) {
        tc_state_load(start, n);
        tc_set_halt_ff(ff);
        double t0 = wall_s();
        uint64_t done = tc_catchup(ticks, NULL, NULL, 0);
        double w = wall_s() - t0;
        printf("%s %s halt ff %s: %.0f emulated s in %.2f wall s = %.0fx\n", argv[1], tc_model_name(model),
               ff ? "on " : "off", (double)done / TC_TICK_HZ, w, w > 0 ? (double)done / TC_TICK_HZ / w : 0);
    }
    tc_set_halt_ff(false);
    free(start);
    return 0;
}
