// SPDX-License-Identifier: GPL-3.0-or-later
// Replays a save from the ball on the PC with a button script and prints every distinct screen.
// A dev tool, not a test: it answers "what does the pet do if I press this?" without touching the ball.
//
//   save_replay <rom.bin> <save.sav|-> "<script>" [hold_ms gap_ms]
//
// The save is the file from GET /api/pets/<id>/save (or the panel's "Back up save"); "-" starts from the ROM's
// power-on state (a fresh egg).
// Script tokens, separated by spaces: A, B, C, AC press like the agent's press tool (hold, then gap);
// wN waits N ms; ffN fast-forwards N emulated seconds without printing frames (the last frame is printed
// after); ram prints every RAM nibble that changed since the previous ram (the first ram prints them all).
// Example: "w1000 A A A A A A B A A" opens the meter and pages to hunger.
// Example: "w1000 ram A B B w8000 ram" shows which nibbles a meal changes (see docs/pet-ram.md).
// clock prints the pet's clock and state ("=== clock at <ms> ms: HH:MM:SS state=<0x5E> char=<0x5D> age=<years>").
// setHHMMSS sets the clock the way the ball's Pet clock action does (tc_clock_request: written at the quiet point
// after the next seconds tick), then prints when it applied, or "busy" (the ROM's own clock menu stops the clock).
// hN sets the hold time of the presses that follow (the P1 clock menu wants A+C held ~3 s: "h3000 AC h200").
// Example: "ff120 clock set210000 clock ff600 clock" moves an awake P1 into the night; ten minutes later it sleeps.
// "@file" in place of the script reads it from that file (for scripts too long for one argument).
// dump <file> writes the state at that point as a save record (pet 0, untrusted time 0) that this tool loads again,
// so long research runs can be chained.
// watchN prints every RAM nibble write during the next N emulated seconds, as the script goes on:
// "watch <ms> 0x<addr>: <old>-><new> pc=<PC of the instruction that wrote it>" (research: which code keeps which
// stat; read straight from tamalib's state after each instruction).
// Emulation runs as fast as possible; times in the output are emulated milliseconds.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "persist.h"
#include "tama_core.h"
#include "tc_pet.h"
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

// ---- watch: RAM writes with the PC that made them ----
static double g_watch_until = -1;
static uint8_t g_shadow[MEM_RAM_SIZE];
static uint16_t g_prev_pc;

static void watch_hook(void)  // after every instruction (tc_debug_set_step_hook)
{
    state_t *st = cpu_get_state();
    if (g_ms <= g_watch_until) {
        for (int a = 0; a < MEM_RAM_SIZE; a++) {
            uint8_t v = GET_RAM_MEMORY(st->memory, a) & 0xF;
            if (v != g_shadow[a]) {
                printf("watch %.0f ms 0x%03X: %X->%X pc=%04X\n", g_ms, a, g_shadow[a], v, g_prev_pc);
                g_shadow[a] = v;
            }
        }
    }
    g_prev_pc = *st->pc;  // the next instruction: the writer, as seen by the next call
}

static void watch_start(double seconds)
{
    state_t *st = cpu_get_state();
    for (int a = 0; a < MEM_RAM_SIZE; a++) g_shadow[a] = GET_RAM_MEMORY(st->memory, a) & 0xF;
    g_prev_pc = *st->pc;
    g_watch_until = g_ms + seconds * 1000;
    tc_debug_set_step_hook(watch_hook);
    printf("=== watch %.0f s from %.0f ms\n", seconds, g_ms);
}

static tc_model_t g_model;

static void show_clock(void)
{
    uint8_t ram[TC_PET_RAM_NIBBLES];
    size_t n = tc_get_ram(ram, sizeof ram);
    uint32_t s;
    printf("=== clock at %.0f ms: ", g_ms);
    if (tc_pet_clock(g_model, ram, n, &s)) printf("%02u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
    else printf("unknown");
    if (n == sizeof ram) printf(" state=%u char=%u age=%u", ram[0x5E], ram[0x5D], ram[0x55] * 10u + ram[0x54]);
    printf("\n");
}

static bool set_clock(const char *t)
{
    if (strlen(t) != 6) return false;
    int v[6];
    for (int i = 0; i < 6; i++) {
        if (t[i] < '0' || t[i] > '9') return false;
        v[i] = t[i] - '0';
    }
    int h = v[0] * 10 + v[1], m = v[2] * 10 + v[3], sec = v[4] * 10 + v[5];
    if (h > 23 || m > 59 || sec > 59) return false;
    uint32_t id = tc_clock_request_ms((uint32_t)(h * 3600 + m * 60 + sec) * 1000u), w = 0;
    tc_clock_state_t st;
    while ((st = tc_clock_poll(id, &w)) == TC_CLOCK_PENDING) run_ms(10);
    if (st == TC_CLOCK_APPLIED) printf("=== set %02u:%02u:%02u applied at %.0f ms\n", w / 3600, w / 60 % 60, w % 60, g_ms);
    else printf("=== set busy at %.0f ms\n", g_ms);
    return true;
}

static uint8_t g_sha1[20];

static bool dump_state(const char *path)
{
    static uint8_t state[1024], out[1024 + PS_OVERHEAD];
    size_t n = tc_state_save(state, sizeof state);
    ps_meta_t m = { .pet_id = 0, .time_trusted = false, .unix_time = 0, .seq = 1 };
    memcpy(m.rom_sha1, g_sha1, 20);
    size_t len = n ? ps_record_encode(&m, state, n, out, sizeof out) : 0;
    FILE *f = len ? fopen(path, "wb") : NULL;
    if (!f || fwrite(out, 1, len, f) != len) { perror(path); if (f) fclose(f); return false; }
    fclose(f);
    printf("=== dumped to %s at %.0f ms\n", path, g_ms);
    return true;
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
        fprintf(stderr, "usage: %s <rom.bin> <save.sav|-> \"<script>\"|@script.txt [hold_ms gap_ms]\n"
                        "script tokens: A B C AC (press), wN (wait N ms), ffN (fast-forward N s quietly),\n"
                        "  ram (RAM nibbles changed since the last ram; the first prints all),\n"
                        "  clock (the pet's clock), setHHMMSS (set it), hN (hold time of later presses),\n"
                        "  dump <file> (write the state as a save), watchN (RAM writes with their PC for N s)\n",
                argv[0]);
        return 2;
    }
    double hold = argc > 5 ? atof(argv[4]) : 200, gap = argc > 5 ? atof(argv[5]) : 400;  // the press tool's defaults

    static uint8_t rom[TC_ROM_MAX_BYTES], rec[4096];
    static uint16_t program[TC_ROM_MAX_WORDS];
    size_t rom_len, rec_len;
    if (!read_file(argv[1], rom, sizeof rom, &rom_len) || !tc_rom_decode(rom, rom_len, program)) {
        fprintf(stderr, "%s: not a ROM dump of a supported size\n", argv[1]);
        return 1;
    }
    g_model = tc_rom_identify(rom, rom_len, g_sha1);
    static const tc_platform_t plat = { now_us, sleep_until, NULL, NULL, NULL };
    if (!tc_init(program, &plat)) return 1;
    tc_set_icon_pins(tc_model_icon_pins(g_model));
    if (tc_pet_clock_addr(g_model) >= 0) tc_set_clock_addr((uint16_t)tc_pet_clock_addr(g_model));
    tc_set_clock_12h(tc_pet_clock_12h(g_model));  // as the ball (app_main.c)
    tc_set_k13_pullup(tc_model_k13_pullup(g_model));
    ps_meta_t meta;
    const uint8_t *payload;
    size_t payload_len;
    if (strcmp(argv[2], "-") != 0 &&
        (!read_file(argv[2], rec, sizeof rec, &rec_len) || !ps_record_decode(rec, rec_len, &meta, &payload, &payload_len) ||
         !tc_state_load(payload, payload_len))) {
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
        if (!strncmp(t, "watch", 5)) { watch_start(atof(t + 5)); continue; }  // before "w": both start with w
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
        if (!strcmp(t, "clock")) { show_clock(); continue; }
        if (!strcmp(t, "dump")) {
            char *path = strtok(NULL, " \t\r\n");
            if (!path || !dump_state(path)) return 1;
            continue;
        }
        if (!strncmp(t, "set", 3) && set_clock(t + 3)) continue;
        if (t[0] == 'h' && t[1] >= '0' && t[1] <= '9') { hold = atof(t + 1); continue; }
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
