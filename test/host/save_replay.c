// SPDX-License-Identifier: GPL-3.0-or-later
// Replays a save from the ball on the PC with a button script and prints every distinct screen.
// A dev tool, not a test: it answers "what does the pet do if I press this?" without touching the ball.
//
//   save_replay <rom.bin> <save.sav> "<script>" [hold_ms gap_ms]
//
// The save is the file from GET /api/pets/<sha1>/save (or the panel's "Back up save").
// Script tokens, separated by spaces: A, B, C, AC press like the agent's press tool (hold, then gap);
// wN waits N ms; ffN fast-forwards N emulated seconds without printing frames (the last frame is printed
// after); ram prints every RAM nibble that changed since the previous ram (the first ram prints them all).
// Example: "w1000 A A A A A A B A A" opens the meter and pages to hunger.
// Example: "w1000 ram A B B w8000 ram" shows which nibbles a meal changes (see docs/pet-ram.md).
// "@file" in place of the script reads it from that file (for scripts too long for one argument).
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
static bool g_quiet;  // ff: no frame output
static uint8_t g_ram[TC_RAM_NIBBLES];
static bool g_have_ram;

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

// In 10 ms steps, so presses and frames line up with the ball's press runner. Ticks are counted from the
// start, one instruction at a time, so emulated time doesn't drift (an instruction is several ticks).
static void run_ms(double ms)
{
    static uint64_t ticks_done;
    for (double d = 0; d < ms; d += 10) {
        g_ms += 10;
        while (ticks_done < (uint64_t)(g_ms * 32.768)) {  // 32768 Hz
            uint32_t t0 = tc_tick_counter();
            tc_step(1);
            ticks_done += (uint32_t)(tc_tick_counter() - t0);
        }
        if (!g_quiet) show_if_changed();
    }
}

static void show_ram(void)
{
    uint8_t now[TC_RAM_NIBBLES];
    size_t n = tc_get_ram(now, sizeof now);
    printf("=== ram at %.0f ms\n", g_ms);
    if (!g_have_ram) {
        for (size_t a = 0; a < n; a++) {
            if (a % 32 == 0) printf("0x%03zX:", a);
            printf("%s%X", a % 4 ? "" : " ", now[a]);
            if (a % 32 == 31 || a + 1 == n) putchar('\n');
        }
    } else {
        for (size_t a = 0; a < n; a++)
            if (now[a] != g_ram[a]) printf("0x%03zX: %X->%X\n", a, g_ram[a], now[a]);
    }
    memcpy(g_ram, now, n);
    g_have_ram = true;
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
        fprintf(stderr, "usage: %s <rom.bin> <save.sav> \"<script>\"|@script.txt [hold_ms gap_ms]\n"
                        "script tokens: A B C AC (press), wN (wait N ms), ffN (fast-forward N s quietly),\n"
                        "  ram (RAM nibbles changed since the last ram; the first prints all)\n", argv[0]);
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

    char *script;
    if (argv[3][0] == '@') {  // long scripts (multi-day care loops) come from a file
        static char buf[1 << 22];
        size_t len;
        if (!read_file(argv[3] + 1, (uint8_t *)buf, sizeof buf - 1, &len)) return 1;
        buf[len] = 0;
        script = strdup(buf);
    } else {
        script = strdup(argv[3]);
    }
    for (char *t = strtok(script, " \t\r\n"); t; t = strtok(NULL, " \t\r\n")) {
        if (t[0] == 'w') { run_ms(atof(t + 1)); continue; }
        if (!strncmp(t, "ff", 2)) {
            printf("=== fast-forward %ss at %.0f ms\n", t + 2, g_ms);
            g_quiet = true;
            run_ms(atof(t + 2) * 1000);
            g_quiet = false;
            show_if_changed();
            continue;
        }
        if (!strcmp(t, "ram")) { show_ram(); continue; }
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
