<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Where P1 and P2 keep the pet's stats in RAM

The ball's glance screen reads the pet's stats straight from the emulator's RAM (`tc_get_ram`, one 4-bit nibble per address). This page lists the addresses and how each was checked. Every row was checked on the PC by replaying one of the owner's saves with `build-host/save_replay` and comparing the RAM against the ROM's own screens (the meter, the clock, the sprites).

**Credit.** The P1 starting point came from the [tamagotchi-carebot](https://github.com/Sagestum/tamagotchi-carebot) README, which publishes P1 addresses found by watching and setting RAM in an emulator: hunger `0x40`, happy `0x41`, care mistakes `0x42`, discipline `0x43`, weight `0x46`/`0x47`, light `0x4B`, droppings `0x4D`, character `0x5D`, call icon `0x2D`, the clock at `0x10`-`0x15`, and, unverified there, "was sick" `0x49` and age `0x54`. Only those facts were used; no code was copied. Sleep, current sickness and the P2 addresses were found here.

## The tables

The two ROMs keep everything the glance needs at the same addresses. "v" is the nibble's value, 0-15.

| Name | Address | Encoding | Verified how (scripts below) |
|---|---|---|---|
| Hunger | `0x40` | hearts = (v+1)/4, so 0-2 → 0, 3-6 → 1, 7-10 → 2, 11-14 → 3, 15 → 4. A meal adds 4 (capped at 15); it drops by 4 over time | P1: meter matched at v = 0, 3, 7, 11, 15 (`MEAL`, `METER1H`, `METER2H`). P2: v = 0, 4, 12, 15 |
| Happy | `0x41` | as hunger. A snack adds 4 | P1: meter matched at v = 0, 4, 8. P2: v = 0, 5, 15 |
| Discipline | `0x43` | quarters of the bar = (v+1)/4. Each discipline of a fake call adds 4 (0, 4, 8, 12); the ROM may set 15 or drop it to 8 when the pet evolves | P1: meter 0 → empty, 4 → 1/4, 12 → 3/4 (`DISC`), 15 → full (set by an evolution, `LONG`). P2: 0 → empty, 12 → 3/4 (`P2METER`) |
| Age | `0x54` ones, `0x55` tens (BCD) | years = 10 × `0x55` + `0x54`. Goes up when the pet wakes in the morning | P1: meter 2 and 3; at the tenth wake-up `0x54` 9→0 and `0x55` 0→1, meter 10 (`LONG`). P2: meter 0 and 2 |
| Weight | `0x46` ones, `0x47` tens (BCD) | 0-99 (lb on P1, g on P2); stops at 99 | P1: meter 22, then 23 after one meal (`0x46` 2→3); 99 after many snacks (`LONG`). P2: meter 5 and 86 |
| Droppings | `0x4D` | number of piles; the screen shows at most 4 (5 was seen in RAM with 4 on screen) | Both: piles on screen matched v = 1, 2, 4 (P1) and 1 (P2); the bathroom sets it to 0 |
| Sick | `0x48` | sick while v ≥ 8. Healthy is 7 (0 on a P2 that was never sick, and after death); sickness sets 15; each medicine dose takes 4 off, so two doses cure (15 → 11 → 7) | Both: skull on screen with v = 15, still sick at 11, skull gone and pet back to normal at 7 (`SICK1`, `SICK2`). A sick pet that falls asleep keeps v = 15 and is sick again when it wakes |
| Asleep | `0x5E` = 3 | `0x5E` is the pet's state: 1 normal, 2 eating, 3 asleep, 5 playing, 9 sick (awake), 10 dead. Asleep only when v = 3 | P1: 1→3 at 20:00 and 3→1 at 09:00, eyes closed and the call for the light at bedtime; stays 3 while a menu is open (`SLEEP1`). P2: 3 for the baby's nap and at night, with Z's on screen |
| Light | `0x4B` | 15 on, 0 off | Both: light menu → off sets 0 (screen dark); P1 also while asleep, which clears the call |
| Character | `0x5D` | 0 = egg or dead; any other value = a living pet (the stage number) | P1: 2 in the saves, 4 and 8 after evolutions, 0 after death. P2: 0 for the egg in `p2.sav`, 1 at hatching, 2 and 4 after evolutions |
| Call icon | `0x2D` bit 3 | 8 while the attention icon is lit | Both: matched the `attention` icon throughout the long runs. Not needed by the glance |
| Clock | `0x10` s ones, `0x11` s tens, `0x12` min ones, `0x13` min tens (BCD); `0x14` hour low, `0x15` hour high (hour = 16 × `0x15` + `0x14`, 0-23) | the pet's own clock | P1: the clock screen read 9:58 with 09:58:00 in RAM; ten emulated minutes later 10:08:00, an hour later 10:58:00; hours 15→16 set `0x14` F→0 and `0x15` 0→1; 23→0 cleared both. P2: clock screen 6:09 PM with 18:09:44 in RAM; +1 h gave 19:09:44 |

Also seen, not used by the glance: care mistakes `0x42` (counts up when a call is ignored, as carebot says), `0x49` (set to 1 when the pet gets sick, cleared at the next evolution, as carebot says).

**Not used:** nothing. Every stat the glance wants was found on both ROMs. The formulas were checked on the meter at the values listed, not at all sixteen: hunger and happy at 0, 3, 4, 5, 7, 8, 11, 12 and 15 between the two ROMs, discipline at 0, 4, 12 and 15.

**Egg and dead.** `0x5D` = 0 means there is no pet to report on: show no stats. The P2 save is an egg (`0x5D` = 0, buttons do nothing until it hatches about five minutes later). A dead P1 pet also reads 0 (with `0x5E` = 10).

**What `0x5E` hides.** While the pet eats or plays, `0x5E` is 2 or 5 even if it is sick or would be asleep, so use `0x48` for sickness (it does not change during animations) and treat "asleep" as `0x5E` = 3 only.

## The saves as they are

Meter readings of the unmodified saves (Task 3's tests compare against these):

| Save | ROM | Hunger | Happy | Discipline | Age | Weight | Other |
|---|---|---|---|---|---|---|---|
| `roms/saves/p1.sav` | P1 | 3 hearts (`0x40` = 11) | 2 hearts (`0x41` = 8) | empty (`0x43` = 0) | 2 | 22 lb (`0x47` 2, `0x46` 2) | `0x5D` = 2, light on, no droppings, not sick (`0x48` = 7), awake (`0x5E` = 1), clock 09:58:00 |
| `roms/saves/now.sav` | P1 | 4 hearts (15) | 2 hearts (8) | empty (0) | 2 | 23 lb | same pet after a meal |
| `roms/saves/p2.sav` | P2 | egg: no meter | | | | | `0x5D` = 0, `0x40` = 1, `0x41` = 1, `0x46` = 5, light on, clock 18:09:44 |

## The scripts

All runs are `build-host/save_replay <rom> roms/saves/<save>.sav "<script>"` (P1 ROM `build-host/roms/tama.bin`, P2 `build-host/roms/tamag2.bin`); a script too long for the command line goes in a file passed as `@file`. `ram` prints the nibbles that changed since the previous `ram` (the first prints all of them); `ffN` runs N emulated seconds without input. Press timing matters: after a fast-forward the pet may be mid-animation, so the long scripts wait 800-1200 ms between presses and check `icons:` in the output.

Building blocks used below:

- `M` (open the meter): `A w800 A w800 A w800 A w800 A w800 A w800 B w1500`, then `A w1500` per page (discipline, hunger, happy).
- `D` (discipline): `A w800` seven times, `B w5000 C w1000 C w1000`. With no call it changes nothing.
- `CARE` (one quarter-hour of care): `ff900 w100 C w1000 C w1000 D ram A w800 B w1500 B w7000 A w1000 B w7000 C w1000 C w1000` (meal, snack), then bathroom `A w800`×5 `B w6000 C w1000 C w1000`.
- `CARE+MED`: `CARE` followed by medicine twice (`A w800`×4 `B w8000 C w1000 C w1000`). `LONG` is `CARE+MED` with `ff1800` in place of `ff900`.

| Name | Save | Script | What it shows |
|---|---|---|---|
| `MEAL` | p1 | `w1000 ram A B w1500 ram B w8000 ram C w1000 ram`, then with `C w500 M A A` appended for the meter | `0x40` 11→15, `0x46` 2→3; meter 4 hearts, 23 lb |
| `METER1H` | p1 | `ff3600 w100 C w1000 C w1000 ram M A A A` | `0x40` = 7, `0x41` = 4; meter 2 and 1 hearts |
| `METER2H` | p1 | `ff7200 w100 C w1000 C w1000 ram M A A A` | `0x40` = 3, `0x41` = 0; meter 1 and 0 hearts |
| light | p1, p2 | `w1000 ram A A B w1000 A w1000 ram B w3000 ram` (P2: `ff360 w100 C w1000` first, to hatch, and `w800` between presses) | `0x4B` 15→0, screen dark |
| neglect | p1 | `w100 ram` + `ff1800 ram` ×56 | droppings 1→5, hunger and happy to 0, care mistakes up, sleep at 20:00, age 2→3 at 09:00, death at about 26 h (`0x5D` → 0, `0x5E` → 10); clock rollovers |
| `DISC` | p1 | `w100 ram` + `CARE` ×30 (with `D` in each), then `M A` | `0x43` 0→4→8→12 at each fake call (attention with full hearts); meter 3/4 |
| `SICK1` | p1 | `w100 ram` + `CARE` ×100, `ff900 w100 C w1000 C w1000 ram`, then medicine with `ram` after each dose | at 97 123 s `0x48` 7→15, `0x5E` 1→9, skull on screen; doses: 15→11, then 11→7 and `0x5E` 9→1 |
| `SLEEP1` | p1 | `ff35940 ram ff60 ram ff60 ram ff60 ram`; `ff82860 ram ff120 ram`; `ff36300 ram A w1200 A w1200 B w2000 A w1500 B w3000 ram` | `0x5E` 1→3 at 20:00, eyes closed two minutes later; 3→1 at 09:00 with `0x54` 2→3; light off while asleep |
| `LONG` | p1 | `w100` + `LONG` ×364, then `M` (file script) | age 9→10 as `0x54` 9→0, `0x55` 0→1; meter at 690 524 s: age 10, 99 lb, discipline bar full (`0x43` = 15, set at an evolution) |
| hatch | p2 | `w100 ram` + `ff60 ram` ×20 | `0x5D` 0→1 at about 5 min |
| `P2MEAL` | p2 | `ff360 w100 C w1000 A w800 B w1500 B w8000 C w1000 C w1000` then `M A A A`; the same with more `B w8000` (meals) or `A w1000` before them (snacks) | one meal `0x40` 0→4 (1 heart), three meals 12 (3 hearts), two snacks `0x41` 1→5 (1 heart); meter 5 g, age 0, discipline empty |
| `SICK2` | p2 | `w100 ram ff360` + `CARE+MED` ×2 + `CARE` up to its `ram`, then medicine ×3 (`A w1200`×4 `B w9000 ram C w1000 C w1000`) | `0x48` 0→15, `0x5E` 1→9, skull; 15→11, then 11→7 and `0x5E` 9→1 |
| P2 night | p2 | `w100 ram ff7200 ram ff46000 ram ff900 ram` | sick pet falls asleep: `0x5E` 9→3 with `0x48` still 15; at 09:00 `0x5E` 3→9 and `0x54` 1→2 |
| `P2METER` | p2 | `w100 ram ff360` + `CARE+MED` ×82, then `M A A A` (file script) | meter: age 2, 86 g, discipline 3/4 (`0x43` = 12), 4 and 4 hearts (`0x40` = `0x41` = 15) |
