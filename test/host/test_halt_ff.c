// SPDX-License-Identifier: GPL-3.0-or-later
// Halt fast-forward (tamalib_set_halt_ff) during catch-up must leave a pet exactly where stepping through every
// HALT leaves it. One ROM per run (TAMAORB_HALTFF_ROM: tama.bin, tamag2.bin, tamaang.bin, tamamot.bin), since
// tc_init runs once per process. P1 and P2 start from the owner's saves (roms/saves/p1.sav, p2.sav), Angel and
// Mothra from a fresh egg out of its clock setup. Skipped (exit 77), loudly, without the dump or the save.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "persist.h"
#include "remote.h"
#include "tama_core.h"
#include "tc_pet.h"
#include "tc_rom.h"
#include "tc_time.h"
#include "test_support.h"
#include "unity.h"

static uint64_t now_us(void) { return 0; }
static void sleep_until_us(uint64_t ts) { (void)ts; }

static const char *s_skip;  // why the tests can't run, or NULL
static tc_model_t s_model;
static uint8_t *s_start;    // the state every run begins from
static size_t s_state_len;

void setUp(void) {}
void tearDown(void) {}

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
    const char *root = getenv("TAMAORB_ROOT");
    char path[512];
    snprintf(path, sizeof path, "%s/roms/saves/%s", root ? root : TAMAORB_ROOT_DEFAULT, file);
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

// Catch-up from the start state, in the given hour stops; the state after each stop goes to out[i].
#define STOPS 3
static const uint32_t STOP_H[STOPS] = { 1, 6, 26 };  // 26 h: about when a neglected P1 dies (docs/pet-ram.md)

static void run_stops(bool halt_ff, uint8_t *out[STOPS], uint32_t ticks_out[STOPS])
{
    TEST_ASSERT_TRUE(tc_state_load(s_start, s_state_len));
    tc_set_halt_ff(halt_ff);
    uint32_t done_h = 0;
    for (int i = 0; i < STOPS; i++) {
        tc_catchup((uint64_t)(STOP_H[i] - done_h) * 3600u * TC_TICK_HZ, NULL, NULL, 0);
        done_h = STOP_H[i];
        TEST_ASSERT_EQUAL(s_state_len, tc_state_save(out[i], s_state_len));
        ticks_out[i] = tc_tick_counter();
    }
    tc_set_halt_ff(false);
}

static void test_halt_ff_exact(void)
{
    if (s_skip) TEST_IGNORE_MESSAGE(s_skip);
    uint8_t *off[STOPS], *on[STOPS];
    uint32_t t_off[STOPS], t_on[STOPS];
    for (int i = 0; i < STOPS; i++) {
        off[i] = malloc(s_state_len);
        on[i] = malloc(s_state_len);
    }
    run_stops(false, off, t_off);
    run_stops(true, on, t_on);
    for (int i = 0; i < STOPS; i++) {
        char msg[96];
        size_t at = 0;
        while (at < s_state_len && off[i][at] == on[i][at]) at++;
        snprintf(msg, sizeof msg, "%u h: states differ from byte %zu (of %zu)", (unsigned)STOP_H[i], at, s_state_len);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(t_off[i], t_on[i], msg);
        TEST_ASSERT_EQUAL_MESSAGE(s_state_len, at, msg);
    }
    for (int i = 0; i < STOPS; i++) {
        free(off[i]);
        free(on[i]);
    }
}

typedef struct { uint64_t max_done, total; } seen_t;
static bool note(uint64_t done, uint64_t total, void *ctx)
{
    seen_t *s = ctx;
    if (done > s->max_done) s->max_done = done;
    s->total = total;
    return true;
}

// The limit is the target: a skipped HALT never carries the replay past it (nor far past a progress call).
#define SLACK_TICKS 128u
static void test_halt_ff_stops_at_target(void)
{
    if (s_skip) TEST_IGNORE_MESSAGE(s_skip);
    TEST_ASSERT_TRUE(tc_state_load(s_start, s_state_len));
    tc_set_halt_ff(true);
    seen_t seen = { 0, 0 };
    uint64_t target = 3600ull * TC_TICK_HZ;
    uint64_t done = tc_catchup(target, note, &seen, TC_TICK_HZ * 10);
    tc_set_halt_ff(false);
    TEST_ASSERT_TRUE(done >= target);
    TEST_ASSERT_TRUE(done - target <= SLACK_TICKS);
    TEST_ASSERT_TRUE(seen.max_done <= seen.total + SLACK_TICKS);
}

int main(void)
{
    const char *name = getenv("TAMAORB_HALTFF_ROM");
    static uint8_t rom[TC_ROM_MAX_BYTES];
    static uint16_t program[TC_ROM_MAX_WORDS];
    static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us };
    size_t len = 0;
    if (name)
        for (size_t sz = TC_ROM_BYTES_S46; !len && sz <= TC_ROM_BYTES_S48; sz += TC_ROM_BYTES_S48 - TC_ROM_BYTES_S46)
            if (test_load_file(name, rom, sz)) len = sz;
    s_model = len ? tc_rom_identify(rom, len, NULL) : TC_MODEL_UNKNOWN;
    static char why[96];
    if (!name) s_skip = "TAMAORB_HALTFF_ROM not set";
    else if (!tc_model_playable(s_model) || !tc_rom_decode(rom, len, program) || !tc_init(program, &plat)) {
        snprintf(why, sizeof why, "no dump: %s", name);
        s_skip = why;
    }
    if (!s_skip) {
        tc_set_realtime(false);
        tc_set_icon_pins(tc_model_icon_pins(s_model));
        tc_set_clock_addr((uint16_t)tc_pet_clock_addr(s_model));
        tc_set_clock_12h(tc_pet_clock_12h(s_model));    // the ball's setup (app_main.c): the proof must run it
        tc_set_k1_pullup(tc_model_k1_pullup(s_model));
        const char *save = s_model == TC_MODEL_P1 ? "p1.sav" : s_model == TC_MODEL_P2 ? "p2.sav" : NULL;
        if (save && !load_save(save)) {
            snprintf(why, sizeof why, "no save: roms/saves/%s", save);
            s_skip = why;
        } else if (!save) {
            run_ms(3000);
            play(tc_pet_setup_exit(s_model));  // out of the clock setup: the egg's clock ticks
            run_ms(60000);
        }
    }
    if (!s_skip) {
        s_state_len = tc_state_size();
        s_start = malloc(s_state_len);
        TEST_ASSERT_EQUAL(s_state_len, tc_state_save(s_start, s_state_len));
    }
    UNITY_BEGIN();
    RUN_TEST(test_halt_ff_stops_at_target);
    RUN_TEST(test_halt_ff_exact);
    int failed = UNITY_END();
    return s_skip && !failed ? 77 : failed;  // 77: ctest's SKIP_RETURN_CODE, so a missing dump or save shows "Skipped"
}
