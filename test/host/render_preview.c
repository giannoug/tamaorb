// SPDX-License-Identifier: GPL-3.0-or-later
// Writes a PPM of one screen for eyeballing. Not a test.
// Usage: render_preview out.ppm [pet|catchup|catchup-confirm|resume|resume-back|battery|message] [held_mask]
//                       [palette 0-2] [frame.txt]
//        render_preview out.ppm pet-<p1|p2|angel|mothra>: that model's pet screen, every icon lit
//        frame.txt (pet only): 16 lines of 32 '#'/'.' then "icons <hex>"; default: a sample face, Attention lit
//        render_preview out.ppm <screen> [palette 0-2] [scroll px] [t_ms]
//        screens: glance, glance-asleep, glance-unknown, settings, sound, brightness, dimming, palette,
//                 tzregion, tzcity, panel, panel-nowifi, rom-first (sample values from the design canvas),
//                 pets, pet-sheet, pet-sheet-running, pet-delete, new-pet, new-confirm (canvas row "Pets"),
//                 clock, clock-busy, clock-notime, clock-confirm (Pet clock: pet 09:58:00, ball 14:32:05)
#include <stdio.h>
#include <stdlib.h>
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
    const int pal = argc > 3 ? atoi(argv[3]) : 0, scroll = argc > 4 ? atoi(argv[4]) : -1;
    const uint32_t t_ms = argc > 5 ? (uint32_t)atoi(argv[5]) : 0;

    static const struct { const char *name; ui_page_t page; int scroll, pressed; } SCREENS[] = {
        { "settings", UI_PAGE_LIST, 0, UI_ROW_BRIGHTNESS }, { "sound", UI_PAGE_SOUND, 0, -1 },
        { "brightness", UI_PAGE_BRIGHTNESS, 0, -1 }, { "dimming", UI_PAGE_DIMMING, 0, -1 },
        { "palette", UI_PAGE_PALETTE, 0, -1 }, { "tzregion", UI_PAGE_TZ_REGION, 32, -1 },
        { "tzcity", UI_PAGE_TZ_CITY, 0, -1 }, { "panel", UI_PAGE_PANEL, 0, -1 }, { "panel-nowifi", UI_PAGE_PANEL, 0, -1 },
        { "clock", UI_PAGE_CLOCK, 0, -1 }, { "clock-busy", UI_PAGE_CLOCK, 0, -1 }, { "clock-notime", UI_PAGE_CLOCK, 0, -1 },
        { "clock-confirm", UI_PAGE_CLOCK_CONFIRM, 0, -1 },
    };
    st_settings_t cfg;
    st_defaults(&cfg, 20);
    cfg.muted = true;
    cfg.brightness = 60;
    cfg.dim_level = 30;
    cfg.dim_usb_s = 0;
    cfg.dim_battery_s = 30;
    cfg.off_battery_s = 60;
    cfg.palette = (st_palette_t)(pal >= 0 && pal <= 2 ? pal : 0);
    strcpy(cfg.tz, "Europe/Isle_of_Man");
    // The panel page's QR: a fixed stand-in grid (finder squares and a pattern), not a scannable code.
    static ui_qr_t qr = { .size = 25 };
    for (int y = 0; y < 25; y++)
        for (int x = 0; x < 25; x++) {
            int fx = x < 7 ? x : x - 18, fy = y < 7 ? y : y - 18;
            bool finder = (x < 7 || x >= 18) && (y < 7 || y >= 18) && !(x >= 18 && y >= 18);
            qr.mod[y][x] = finder ? (fx == 0 || fx == 6 || fy == 0 || fy == 6 || (fx >= 2 && fx <= 4 && fy >= 2 && fy <= 4))
                                  : (uint8_t)(((x * 7) ^ (y * 3)) & 1);
        }
    bool menu = false;
    for (size_t i = 0; i < sizeof SCREENS / sizeof SCREENS[0]; i++)
        if (!strcmp(mode, SCREENS[i].name)) {
            ui_menu_view_t v = { .page = SCREENS[i].page, .scroll = scroll >= 0 ? scroll : SCREENS[i].scroll,
                                 .pressed = SCREENS[i].pressed, .cfg = &cfg, .region = "Europe", .t_ms = t_ms,
                                 .panel_url = strcmp(mode, "panel-nowifi") ? "http://192.168.1.50/" : NULL,
                                 .panel_qr = &qr, .panel_name = "tamaorb.local",
                                 .pet_s = 9 * 3600 + 58 * 60, .ball_s = 14 * 3600 + 32 * 60 + 5,
                                 .confirm_s = 14 * 3600 + 32 * 60 + 5, .clock_busy = !strcmp(mode, "clock-busy"),
                                 .clock_why = strcmp(mode, "clock-notime") ? UI_CLOCK_OK : UI_CLOCK_NO_TIME };
            if (!strcmp(mode, "clock-notime")) v.ball_s = -1;
            if (v.page == UI_PAGE_TZ_CITY && scroll < 0)  // bring the current zone into view, as the canvas does
                for (int k = 0; ui_menu_city_at("Europe", k); k++)
                    if (!strcmp(ui_menu_city_at("Europe", k), cfg.tz)) v.scroll = (k - 2) * 32;
            ui_render_menu(fb, &v, ui_palette_by_id(pal));
            menu = true;
        }
    if (menu) {
    } else if (!strncmp(mode, "glance", 6)) {
        tc_pet_t pet = { .known = true, .hunger = 1, .happy = 3, .discipline = 2, .age_years = 3, .weight = 12,
                         .droppings = 0, .sick = 0, .asleep = 0, .light_on = 0 };
        if (!strcmp(mode, "glance-asleep")) { pet.hunger = 4; pet.asleep = 1; pet.light_on = 1; }
        if (!strcmp(mode, "glance-unknown")) pet.known = false;
        f.rows[0] = 0;  // the canvas pet, no icons on the copy
        ui_glance_t g = { .frame = &f, .pet = &pet, .model = TC_MODEL_P1, .clock_known = true, .hour = 14,
                          .minute = 32, .batt_pct = 80, .address = "tamaorb.local" };
        ui_render_glance(fb, &g, ui_palette_by_id(pal));
    } else if (!strcmp(mode, "catchup")) ui_render_catchup(fb, "P2 #1", 8040, 29100, 760, UI_SKIP_BUTTON, p);
    else if (!strcmp(mode, "catchup-confirm")) ui_render_catchup(fb, "P2 #1", 8040, 29100, 760, UI_SKIP_CONFIRM, p);
    else if (!strcmp(mode, "reset") || !strcmp(mode, "reset-holding")) {
        ui_menu_view_t v = { .page = UI_PAGE_RESET, .pressed = -1, .cfg = &cfg, .hold_pct = strcmp(mode, "reset") ? 60 : 0 };
        ui_render_menu(fb, &v, ui_palette_by_id(pal));
    } else if (!strcmp(mode, "resume") || !strcmp(mode, "resume-back")) {
        ui_resume_view_t v = { "P2 #1", 3 * 86400 + 4 * 3600, 3664, !strcmp(mode, "resume-back") ? "P1 #1" : NULL };
        ui_render_resume(fb, &v, p);
    }
    else if (!strcmp(mode, "battery")) ui_render_low_battery(fb, p);
    else if (!strcmp(mode, "rom-first") || !strcmp(mode, "new-pet") || !strcmp(mode, "new-confirm")) {
        static const ui_rom_row_t roms[] = { { "P1", "Gen 1 - tama.bin", "tama.bin", true },
                                             { "P2", "Gen 2 - tamag2.bin", "tamag2.bin", true },
                                             { "Angel", "tamaang.bin", "tamaang.bin", true },
                                             { "Mothra", "not on the SD card", NULL, false } };
        ui_menu_view_t v = { .page = strcmp(mode, "new-confirm") ? UI_PAGE_ROM : UI_PAGE_ROM_CONFIRM, .pressed = -1,
                             .cfg = &cfg, .roms = roms, .rom_count = 4, .rom_pick = 2,
                             .rom_first = !strcmp(mode, "rom-first"), .new_name = "Angel #2", .running_name = "P1 #1" };
        if (v.rom_first) v.rom_count = 2;
        ui_render_menu(fb, &v, ui_palette_by_id(pal));
    } else if (!strcmp(mode, "pets") || !strcmp(mode, "pet-sheet") || !strcmp(mode, "pet-sheet-running") ||
               !strcmp(mode, "pet-delete")) {
        static const ui_pet_row_t pets[] = { { "P1 #1", "Gen 1 - 3 yr - now", "Gen 1 - 3 yr", "running now", true },
                                             { "Angel #1", "Angel - 1 yr - 5h", "Angel - 1 yr", "away 5h 2m", true },
                                             { "P2 #1", "Gen 2 - egg - 3d", "Gen 2 - egg", "away 3d 4h", true } };
        ui_page_t page = !strcmp(mode, "pets") ? UI_PAGE_PETS : !strcmp(mode, "pet-delete") ? UI_PAGE_PET_DELETE : UI_PAGE_PET_SHEET;
        ui_menu_view_t v = { .page = page, .pressed = -1, .cfg = &cfg, .pets = pets, .pet_count = 3, .pet_current = 0,
                             .pet_pick = !strcmp(mode, "pet-sheet-running") ? 0 : 2, .running_name = "P1 #1" };
        ui_render_menu(fb, &v, ui_palette_by_id(pal));
    } else if (!strcmp(mode, "message")) ui_render_message(fb, "No ROM found", "Put a P1 or P2 dump in\n/tamaorb/roms/ on the SD card,\nor push one over USB.", p);
    else if (!strncmp(mode, "pet-", 4)) {
        static const struct { const char *name; tc_model_t m; } MODELS[] = {
            { "p1", TC_MODEL_P1 }, { "p2", TC_MODEL_P2 }, { "angel", TC_MODEL_ANGEL }, { "mothra", TC_MODEL_MOTHRA } };
        tc_model_t m = TC_MODEL_UNKNOWN;
        for (size_t i = 0; i < sizeof MODELS / sizeof MODELS[0]; i++)
            if (!strcmp(mode + 4, MODELS[i].name)) m = MODELS[i].m;
        if (m == TC_MODEL_UNKNOWN) { fprintf(stderr, "unknown model: %s\n", mode + 4); return 2; }
        f.icons = 0xFF;
        ui_render_pet(fb, &f, m, 0, p);
    } else {
        FILE *in = argc > 5 ? fopen(argv[5], "r") : NULL;
        if (argc > 5 && !in) { perror(argv[5]); return 1; }
        if (in) {
            char line[64];
            unsigned icons = 0;
            memset(&f, 0, sizeof f);
            for (int y = 0; y < 16 && fgets(line, sizeof line, in); y++)
                for (int x = 0; x < 32 && line[x] && line[x] != '\n'; x++)
                    if (line[x] == '#') f.rows[y] |= 1u << x;
            if (fgets(line, sizeof line, in) && sscanf(line, "icons %x", &icons) == 1) f.icons = (uint8_t)icons;
            fclose(in);
        }
        ui_render_pet(fb, &f, TC_MODEL_P1, argc > 3 ? (uint8_t)(argv[3][0] - '0') : 0, ui_palette_by_id(argc > 4 ? atoi(argv[4]) : 0));
    }
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
