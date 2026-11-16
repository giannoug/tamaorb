// SPDX-License-Identifier: GPL-3.0-or-later
// Replays a save from the ball on the PC with a button script and prints every distinct screen.
// A dev tool, not a test: it answers "what does the pet do if I press this?" without touching the ball.
//
//   save_replay <rom.bin> <save.sav> "<script>" [hold_ms gap_ms]
//
// The save is the file from GET /api/pets/<sha1>/save (or the panel's "Back up save").
// Script tokens, separated by spaces: A, B, C, AC press like the agent's press tool (hold, then gap);
// wN waits N ms. Example: "w1000 A A A A A A B A A" opens the meter and pages to hunger.
// Emulation runs as fast as possible; times in the output are emulated milliseconds.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "persist.h"
#include "tama_core.h"
#include "tc_rom.h"

static uint64_t now_us(void) { return 0; }  // fast mode never reads the clock
static void sleep_until(uint64_t ts) { (void)ts; }

static const char *ICONS[8] = {"food", "light", "game", "medicine", "bathroom", "meter", "discipline", "attention"};
static tc_frame_t g_last;
static bool g_have;
static double g_ms;

static void show_if_changed(void)
{
    tc_frame_t f;
    tc_get_frame(&f);
    if (g_have && !memcmp(&f, &g_last, sizeof f)) return;
    g_last = f;
    g_have = true;
    printf("--- t=%.0f ms icons:", g_ms);
    for (int i = 0; i < 8; i++)
        if (f.icons & (1u << i)) printf(" %s", ICONS[i]);
    printf("\n");
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) putchar((f.rows[y] >> x) & 1 ? '#' : '.');
        putchar('\n');
    }
}

static void run_ms(double ms)  // in 10 ms steps, so presses and frames line up with the ball's press runner
{
    for (double d = 0; d < ms; d += 10) {
        uint32_t t0 = tc_tick_counter();
        while ((uint32_t)(tc_tick_counter() - t0) < 328) tc_step(64);  // 328 ticks of 32768 Hz ~ 10 ms
        g_ms += 10;
        show_if_changed();
    }
}

static bool read_file(const char *path, uint8_t *buf, size_t cap, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return false; }
    *len = fread(buf, 1, cap, f);
    fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <rom.bin> <save.sav> \"<script>\" [hold_ms gap_ms]\n", argv[0]);
        return 2;
    }
    double hold = argc > 5 ? atof(argv[4]) : 200, gap = argc > 5 ? atof(argv[5]) : 400;  // the press tool's defaults

    static uint8_t rom[TC_ROM_WORDS * 2], rec[4096];
    static uint16_t program[TC_ROM_WORDS];
    size_t rom_len, rec_len;
    if (!read_file(argv[1], rom, sizeof rom, &rom_len) || !tc_rom_decode(rom, rom_len, program)) {
        fprintf(stderr, "%s: not a P1/P2 ROM dump\n", argv[1]);
        return 1;
    }
    static const tc_platform_t plat = { now_us, sleep_until, NULL, NULL, NULL };
    if (!tc_init(program, &plat)) return 1;
    ps_meta_t meta;
    const uint8_t *payload;
    size_t payload_len;
    if (!read_file(argv[2], rec, sizeof rec, &rec_len) || !ps_record_decode(rec, rec_len, &meta, &payload, &payload_len) ||
        !tc_state_load(payload, payload_len)) {
        fprintf(stderr, "%s: not a TamaOrb save for this build\n", argv[2]);
        return 1;
    }
    tc_set_realtime(false);

    char *script = strdup(argv[3]);
    for (char *t = strtok(script, " "); t; t = strtok(NULL, " ")) {
        if (t[0] == 'w') { run_ms(atof(t + 1)); continue; }
        uint8_t mask = !strcmp(t, "A") ? 1 : !strcmp(t, "B") ? 2 : !strcmp(t, "C") ? 4 : !strcmp(t, "AC") ? 5 : 0;
        if (!mask) { fprintf(stderr, "unknown token: %s\n", t); return 2; }
        printf("=== press %s at %.0f ms\n", t, g_ms);
        tc_post_buttons(mask);
        run_ms(hold);
        tc_post_buttons(0);
        run_ms(gap);
    }
    free(script);
    return 0;
}
