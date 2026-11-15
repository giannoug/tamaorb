// SPDX-License-Identifier: GPL-3.0-or-later
// Writes a PPM of one screen for eyeballing. Not a test.
// Usage: render_preview out.ppm [pet|catchup|battery|picker|message] [held_mask]
#include <stdio.h>
#include <string.h>
#include "ui.h"

static const char *SAMPLE[16] = {
    "................................", "............####................", "..........##....##..............",
    ".........#........#.............", "........#..##..##..#............", ".......#...##..##...#...........",
    ".......#............#...........", ".......#....#..#....#...........", ".......#.....##.....#...........",
    ".......#............#...........", "........#..........#............", "........#..........#............",
    ".........##......##.............", "..........#.####.#..............", "..........#.#..#.#..............",
    "...........#....#..............."};

int main(int argc, char **argv)
{
    static uint16_t fb[UI_W * UI_H];
    tc_frame_t f = { .icons = 1u << 7 };
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if (SAMPLE[y][x] == '#') f.rows[y] |= 1u << x;
    const char *mode = argc > 2 ? argv[2] : "pet";
    const ui_palette_t *p = &UI_PALETTE_CLASSIC;
    if (!strcmp(mode, "catchup")) ui_render_catchup(fb, 8040, 29100, 760, p);
    else if (!strcmp(mode, "battery")) ui_render_low_battery(fb, p);
    else if (!strcmp(mode, "picker")) {
        const char *names[] = { "P1", "P2" }, *subs[] = { "Gen 1 - tama.bin", "Gen 2 - tamag2.bin" };
        ui_render_picker(fb, names, subs, 2, 0, p);
    } else if (!strcmp(mode, "message")) ui_render_message(fb, "No ROM found", "Put a P1 or P2 dump in\n/tamaorb/roms/ on the SD card,\nor push one over USB.", p);
    else ui_render_pet(fb, &f, TC_MODEL_P1, argc > 3 ? (uint8_t)(argv[3][0] - '0') : 0, true, p);
    FILE *o = fopen(argc > 1 ? argv[1] : "pet.ppm", "wb");
    fprintf(o, "P6 %d %d 255\n", UI_W, UI_H);
    for (int i = 0; i < UI_W * UI_H; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 0x3F) << 2), (unsigned char)((c & 0x1F) << 3) };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
    return 0;
}
