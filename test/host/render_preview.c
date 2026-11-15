// SPDX-License-Identifier: GPL-3.0-or-later
// Writes build-host/pet.ppm: the pet screen with a sample frame, for eyeballing. Not a test.
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
    ui_render_pet(fb, &f, TC_MODEL_P1, argc > 2 ? (uint8_t)(argv[2][0] - '0') : 0, true, &UI_PALETTE_CLASSIC);
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
