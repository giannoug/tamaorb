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
| Clock | `0x10` s ones, `0x11` s tens, `0x12` min ones, `0x13` min tens (BCD); `0x14` hour low, `0x15` hour high (hour = 16 × `0x15` + `0x14`, 0-23) | the pet's own clock | P1: the clock screen read 9:58 with 09:58:00 in RAM; ten emulated minutes later 10:08:00, an hour later 10:58:00; hours 15→16 set `0x14` F→0 and `0x15` 0→1; 23→0 cleared both. P2: clock screen 6:09 PM with 18:09:44 in RAM; +1 h gave 19:09:44. Written by the ball's Pet clock action at the quiet point after a seconds tick ("Setting the clock" below) |

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

## Setting the clock

The ball's Pet clock action (`tc_clock_request`) writes `0x10`-`0x15` directly. These runs, on the PC with `save_replay`'s `clock` and `setHHMMSS` tokens, are why that is safe and what it does to the pet. They were made for the clock-sync design (2026-11-17).

**When to write.** At each second the ROM writes `0x10`, then carries into `0x11`-`0x15` within ~25 instructions (~150 ticks of 32 768 Hz, ~5 ms); nothing else touches the clock until the next second. The ball writes 1 638 ticks (~50 ms) after it sees `0x10` change, between two instructions, so it never lands in the middle of a carry. If no tick comes within 2 s (the ROM's own clock menu stops the clock), it gives up.

**What a jump does.** The ROM compares the clock with the pet's night window (20:00-09:00 for the stages tried: P1 stage 2, P2 stages 1-2), not with the exact moment. Hunger, happiness, sickness and death followed elapsed time in every run, not the clock.

| What | Save | Script | Result |
|---|---|---|---|
| carry | p1 | `ff119 w100 ram w980 ram` | at 10:00:00 the second's carry rewrites `0x10`-`0x14` together; `test_clock` checks the write lands ≥ 1 638 ticks after the tick |
| awake into the night | p1 | `ff120 clock set210000 clock ff600 clock` | 21:00:00, still awake (`state=1`); ten minutes later asleep (`state=3`) |
| asleep into the day | p1 | `ff43200 clock set100000 ff60 clock` | asleep at 21:58; set to 10:00 it wakes at once and age goes 2→3 (waking is the birthday) |
| awake, back over 09:00 | p1 | `ff1920 clock set083000 ff600 clock ff1800 clock` | at 08:40 asleep again; at 09:00 it wakes with age 2→3, a second birthday the same day |
| through the ROM's own menu | p1 | `ff39720 clock B w1500 h3000 AC h200` + `A w300` ×22 + `clock C w1000 clock` | asleep at 21:00, set to 19:00 in the menu: on leaving it, age 2→3, as with a direct write |

**The ROM's own clock menu.** P1 and P2 alike: B on the main screen shows the clock; holding A+C about 3 s enters set mode, which sets the seconds to 00 and stops the clock (`0x28`-`0x2B` and `0x2E` are cleared too; no pet stat changes); A steps the hour, B the minute, C leaves and the clock runs again. A set request while the menu is open reports busy, e.g. P2: `ff360 w100 C w1000 B w1500 h3000 AC h200 w2000 set070000 clock` (`=== set busy`, clock still 18:15:00). The one difference from a direct write found: after the menu, an awake pet set into the night stays awake until morning.


## A fresh egg's clock setup (all four ROMs)

A ROM started from power-on (no save) sits in its own clock-setting mode with the clock stopped: P1, P2 and Angel hold an invalid hour (`0x15` = 2, i.e. 32 h) at `0x10`-`0x15`, Mothra 00:00:00 at `0x1A`-`0x1F`, and nothing ticks for an hour of emulated time. **B** (shows the clock), **A** (steps the hour, which also makes P1's stored hour valid) and **C** (leaves) start it, with about 1 s between presses (the press tool's 200 ms hold and 400 ms gap is too quick for P2 and Angel): `save_replay <rom> - "w3000 B A C w1500 clock w3000 clock" 200 1000`. P1, P2 and Angel then run from 01:00:00 at `0x10`, Mothra at `0x1A`; P1's egg hatches about 20 min later (`set120000 ff1200` after the setup shows `char=1`). `tc_pet_setup_exit` holds these presses; `test_setup` checks each dump (skipped without it).

## Angel (`tamaang`, E0C6S48, 16 KB)

Runs from fresh eggs (`save_replay <rom> - …`; there are no owner saves), chained with `dump`. Angel keeps P1's RAM layout for everything found.

| Name | Address | Encoding | Verified how |
|---|---|---|---|
| Clock | `0x10`-`0x15` | as P1 | ticks after the setup exit; `set120000` applies |
| Character | `0x5D` | 0 egg, 1 after hatching, 2 at the first evolution (about 1 h later) | hatch and 2 h runs |
| State | `0x5E` | 1 normal, 2 eating, 3 asleep, 9 sick (awake) | meal 1→2→1; asleep at 20:00 (`set180000` + `ff1800` ×24); sick below |
| Hunger | `0x40` | as P1: a meal adds 4; hearts = (v+1)/4 | meal 0→4, hunger page one heart |
| Happy | `0x41` | a game adds 1 (P1's formula assumed for hearts) | game 0→1 |
| The bar | `0x43` | the meter's second page (a bar, Angel's stand-in for discipline); 4 showed a small fill | meter page with `0x43` = 4 |
| Age | `0x54`, `0x55` | as P1 | meter "0" at 0; 0→1 at the first evolution |
| Weight | `0x46`, `0x47` | as P1; a snack adds 2 | meter "4" with `0x46` = 4; snack 4→6 |
| Sick | `0x48` | ≥ 8 sick: switching the light off by day set 15 with `0x5E` 1→9 | light-off run |
| Light | `0x4B` | 15 on, 0 off | the light menu |
| Care mistakes | `0x42` | counts up while calls are ignored | 2 h run |
| Droppings | `0x4D` | number of piles, as P1; the ROM adds one at a time and stops at 8 (code at `0x0350`-`0x0358`); the bathroom sets 0 (`0x0B6C`) | `ANGPOOP5`: `0x4D` 0→1 at 17.56 h and a pile with stink lines appears at the bottom right. `ANGPOOP6`: 0→1 at 18.65 h with the pile, the next block's bathroom set 0 and the pile was gone (18.70 h) |
| Weight unit | (glyph) | the meter's weight line is a weight icon, the number (`0x47` `0x46`, tens blank when 0), then a **heart**: a filled heart with a three-pixel highlight at its top left (`.##.##.` / `#..####` / `#.#####` / `#######` / `.#####.` / `..###..` / `...#...`). The age line ends in 日 (days) | `ANGMETER`: "28" + heart with `0x47` = 2, `0x46` = 8 (stage 4, 48 h); "4" + heart with `0x46` = 4 (the baby). The ROM writes tile `0x1B` after the weight digits (`0x0866`-`0x086D`) |

Icons (bit = tamalib icon number; the same pictures as P1, in another order on the glass): 0 meter, 1 food, 2 game, 3 bathroom, 4 discipline (shouting mouth), 5 medicine, 6 light, 7 attention. A from the main screen steps bit 0 → 6.

**Why droppings took so long to show.** Angel only makes droppings from its second stage on, and only while it is happy: at each happiness tick, if `0x41` ≥ 4 the ROM takes 4 off and adds about ((`0x41` − 4)/2 + weight tens)/2 + `0x1F` to an accumulator at `0x214` (code `0x0FAB`-`0x0FC0`; with `0x41` = 15 and weight 28 it added 3, `ANGPOOP5`); when that overflows (and `0x5D` ≠ 1) it queues a "make droppings" event (dispatched through `0x5C` at `0x0309`), which adds 1 to `0x4D` (`0x0350`). With happy below 4, as in the earlier neglect and meal-only runs (a game adds only 1), `0x214` never moves and no droppings come. `ANGPOOP5` kept happy up with three games every 20 minutes.

Angel runs (ROM `build-host/roms/tamaang.bin`; scripts and saves in `.superpowers/sdd/2026-11-25-leftovers/research/`, `saves/` and `logs/` there; `angscript.py` writes the scripts):

| Name | Save | Script | What it shows |
|---|---|---|---|
| baby | `-` | `w3000 B w1000 A w1000 C w2000 ff600 w100 ram dump saves/ang_baby.sav` (200 600 timing) | hatched baby, `0x5D` = 1 |
| feed48 | `ang_baby.sav` | `angscript.py feed 48 30 step=300` + `dump saves/ang_feed48.sav` | a meal and a "snack" every 30 min for 48 h: no droppings (happy stays low), stage 4 at 30.9 h |
| `ANGPOOP5` | `ang_feed48.sav` | `angscript.py games 24 20 games=3` | `0x214` steps up at each tick with happy ≥ 4; overflow at 17.56 h → `0x4D` 0→1, pile on screen |
| `ANGPOOP6` | `ang_feed48.sav` | `angscript.py games 36 20 games=3 clean=1` | `0x4D` 0→1 at 18.65 h (pile), bathroom → 0 at 18.70 h (no pile) |
| `ANGMETER` | `ang_feed48.sav`, `ang_baby.sav` | `w1000 ram A w1000 B w2500 A w2500 A w2500 A w2500` (200 800 timing) | first meter page: "4日" over "28♥" (48 h save), "0日" over "4♥" (baby) |

**Not found:** nothing left for the glance.

## Mothra (`tamamot`, E0C6S48, 16 KB)

Mothra's RAM is laid out differently, and its glass wires the icons to segments tamalib doesn't map: TamaOrb reads them from display memory (`tc_model_icon_pins`).

Every row below was checked three ways: the ROM code that keeps or draws the value (a disassembly, `disasm.py`), a save with the nibble poked (`poke.py`, `sweep.py`) and two or more natural runs whose screens (meter pages, sprites) were compared with the RAM at every RAM dump (`mjoin.py` + `mcheck.py` for the meter: 56 visits in `CARE2`-`CARE4`, hunger, happy, bar, age and weight agreed with the RAM dumped just before every time; `snap.py` for the screen). "v" is the nibble's value.

| Name | Address | Encoding | Verified how |
|---|---|---|---|
| Clock | `0x1A`-`0x1F` | s ones, s tens, min ones, min tens (BCD), then a **12-hour** hour: `0x1E` = 1-12, `0x1F` bit 0 = PM. 24 h hour = (`0x1E` mod 12) + 12 × (`0x1F` & 1); midnight is `0x1E` = 12, `0x1F` = 0, noon 12 and 1 | `CLOCK12`: the ROM's own carry goes 11:59 AM (`B`,0) → (`C`,1), (`C`,0) → (1,0), 11:59 PM (`B`,1) → (`C`,0), and the clock-setting menu's A steps 1…11, 12 PM, 1 PM…11 PM, 12 AM. The ROM's sleep check compares `0x1E` (12 read as 0) with the stage's sleep hour when `0x1F` bit 0 is set and with its wake hour when it is clear (`0x099C`) |
| Stage | `0x0B` | 0 egg, 1 baby, 2 child, 3 or 4 teen, 5 cocoon (the teen spins it on a tower for an hour; the adult to come waits in `0x63`), 7/8/9/A adults, B/C/D/E second adult forms, F dead. The evolution code never picks 6 | natural runs: 1→2→3→5→A→F (`CARE3`), 1→2→4→5→A→F (`CARE4`), 1→2→3→5→7 (`CARE5`, no mistakes, bar 4); all adults forced from a teen save (`EVO`) |
| Hunger | `0x3E` (`0x3F`) | hearts = v, at most 4; if `0x3F` ≠ 0 the meter shows 4 (`0x047D`). A meal adds 1 and is refused at 4 (`0x0324`); one comes off per stage tick (child and teen: 49 min) | poke 5 → 4 hearts, `0x3F` = 5 → 4 hearts (`SWEEP`); `MEALS4`: 1, 2, 3, 4 hearts with v = 1-4; every meter visit of `CARE2`-`CARE4` (54, no mismatch) |
| Happy | `0x3D` | hearts = v, at most 4 (`0x0487`). A snack adds 1 (`0x07D5`, at most 4); a won game (about half) adds 1 | poke 5 → 4 hearts (`SWEEP`); `SNACK5`: 1, 2, 3 hearts after 1-3 snacks; every meter visit of `CARE2`-`CARE4` |
| Bar ("せいぎ", justice) | `0x3B` | the meter's fourth page, titled せいぎ (justice): quarters filled = v, full at 4 (drawn 4 columns per unit from `0x0447`, so v > 4 still shows full). Scolding (shouting-mouth icon) while the pet makes its misbehaving call (`0x30` bit 0) adds 1, at most 4 (`0x0396`); each evolution resets it to 0, 2 or 4 depending on the new stage | poke 1-4 → ¼, ½, ¾, full (`SWEEP` + `BAR`); `CARE2` 0→1→2; `CARE4` 0→1→2→3 as a child, 2 at the teen, 3, 4, 0 at the adult A; meter quarters matched at every visit |
| Age | `0x5E` ones, `0x5F` tens (BCD) | days (the meter writes 日 after it); one more each time the pet wakes, the baby's nap included (`0x09D9` → `0x0076`) | poke `0x5E` = 5 → "5", `0x5F` = 5 → "50" (`SWEEP`); `CARE3`: 0→1 at the baby's nap, then +1 at every morning, 9→0 with `0x5F` 0→1 at 217 h; meter matched at every visit |
| Weight | `0x56` ones, `0x57` tens (BCD) | tons (the meter writes "t"). Meals and snacks add, games take 1 off, never below the stage's base (`0x68`/`0x69`), at most 99 | poke `0x56` = A → a non-digit, `0x57` = 5 → "55" (`SWEEP`); `CARE2`: 13, 26 … 91, 99 and stays 99; `CARE4`: 10, 20, 30 at each evolution; meter matched at every visit |
| Sick | `0x05` bit 0 | 1 = sick: the skull (top right) while awake. Each medicine (two-fairies icon) takes one off `0x6A`; when `0x6A` was 0 the bit clears, so it takes `0x6A` + 1 doses (2 for the baby). Bit 1 is set when the pet starts dying (skull too, then `0x0B` = F) | `SICKFIND` (bit set at 1 076 s, skull from then on), `CURE` (dose 1: `0x6A` 1→0, skull; dose 2: bit clear, skull gone); in `CARE4` (1 441 RAM dumps) and the neglect run the skull is on screen exactly when bit 0 or 1 is set and the pet is awake |
| Asleep | `0x05` bit 2 | 1 = asleep (night and the baby's nap). Child and teen sleep 8 PM-8 AM (teen 4: 8 PM-9 AM), adult A 11 PM-10 AM: the hours are `0x66` (PM) and `0x67` (AM), set per stage | Z's on screen with the light on exactly when the bit is set (neglect run: `0x05` = 4 and 5; `CARE2`, `CARE4` baby naps); bit set and cleared at the stage's hours in `CARE2`-`CARE5` |
| Light | `0x08` bit 3 | 1 = off: the whole screen is dark (every pixel lit). The light menu (sun icon) writes it from its cursor; the ROM turns the light on when the pet wakes (`0x09DF`) | `LIGHT1`: off → `0x08` 0→8, screen dark; on → 8→0. `CARE4`: dark screen at exactly the RAM dumps with bit 3 set (750 of 1 441); `CARE5` the same (977 of 1 801, bit 1 set or not). Mask it: bit 1 is the clock's 24-hour display and bit 0 the fast mode below |
| Droppings | `0x07` | number of piles, at most 8 (`0x04E9`); the coil icon cleans (sets 0, `0x04AE`) | `POOP1`: 1, 2, 3, 4 piles on screen with v = 1-4 (one about every 3 h, no cleaning); `CARE4`: 0→1 with a pile, the coil → 0 and the pile gone |
| Care mistakes | `0x37` | +1 each time a call (hunger or happy at 0, light left on at bedtime) runs out; reset at each evolution, which it steers | `CARE4` (≈ 0.7 h apart while happy was 0), `EVO` |

Icon pins (bit: segment-COM, picture): 0: 8-0 circle (meter), 1: 17-0 leaves (food), 2: 18-0 bow (game), 3: 19-0 coil (clean the droppings), 4: 39-8 shouting mouth (scold), 5: 38-8 two fairies (medicine), 6: 37-8 sun (light), 7: 28-8 sparkle (attention). A from the main screen steps bit 0 → 6. The meter's pages: age 日 and weight t, おなか (hunger) hearts, ごきげん (mood) hearts, せいぎ (justice) bar.

**Not like P1.** Hunger, happy and the bar count 0-4 directly (P1: 0-15 and (v+1)/4). Sickness is a flag, not P1's ≥ 8 threshold. There is no state nibble like P1's `0x5E`: sick, dying and asleep are bits 0, 1 and 2 of `0x05`. The light is bit 3 of `0x08` and set means off (P1: `0x4B` 15 on, 0 off). Age is in days and also goes up after the baby's nap. The stage nibble `0x0B` plays P1's `0x5D` (0 egg, F dead). The clock is 12-hour with a PM bit (above). The menus' own cursors: the food menu remembers its item (`0x23`: 1 meal, 2 snack); the light menu starts on the current state. For the glance: **bar label "Justice"** (P1's discipline slot), hunger = min(`0x3E`, 4) (4 if `0x3F` ≠ 0), happy = min(`0x3D`, 4), bar = min(`0x3B`, 4) quarters, sick = `0x05` & 3, asleep = `0x05` & 4, light off = `0x08` & 8, droppings = `0x07`, weight = 10 × `0x57` + `0x56` t, age = 10 × `0x5F` + `0x5E` days, no stats while `0x0B` is 0 or F.

**Stages and how the ROM picks them** (`0x0D10`, a jump table on the current stage; "mistakes" is `0x37`, "bar" `0x3B`, both read at the evolution, which comes after a set awake time kept in `0x48`-`0x4B` and compared with `0x4C`-`0x4F`: about 24 awake hours for the child, 42 for a teen):

| From | To |
|---|---|
| 0 egg | 1 baby (about 5.5 min after the setup exit) |
| 1 baby | 2 child (about 1 h) |
| 2 child | 3 if mistakes < 3, else 4 |
| 3 teen | 7 if bar 4 and no mistakes; else 8 if mistakes < 3 (bar < 2) or < 4 (bar ≥ 2); else A. Through 5 (cocoon) for an hour |
| 4 teen | 8 if bar 4 and mistakes < 2; else 9 if mistakes < 4 (bar < 2) or < 5; else A. Through 5 (cocoon) |
| 7 | D when there are no mistakes and the weight is 70-79 t at two checks in a row (`0x16` counts them, a failed check clears it); E instead if the count is exactly 5 at a check, which these runs never reached naturally (forced with `0x16` = 4) |
| 8 | B if bar 4 and no mistakes |
| A | C if bar 4 and no mistakes |
| 9, B, C, D, E | no further stage |

Also seen, not used by the glance (from the code, matched in the runs): `0x36` sicknesses this stage, `0x38`-`0x3A` sick time, `0x6A` medicine doses still needed − 1; `0x48`/`0x49` awake minutes (BCD) and `0x4A`/`0x4B` awake hours (binary, they stop while asleep), `0x4C`-`0x4F` the stage's target for them; `0x83`/`0x84` minutes until the next hunger heart comes off (reloaded per stage from `0x0600`), `0x86`/`0x87` the same for happy (`0x0610`); `0x7A`/`0x7B` and `0x7C`/`0x7D` count down while hunger or happy is at 0 (a care mistake when they run out), `0x7E`/`0x7F` a third such call; `0x3C` snacks modulo 3 and `0x62` +1 per three snacks (it raises the odds of sickness, `0x0AA9`, and is cleared at evolution); `0x66`/`0x67` the stage's sleep (PM) and wake (AM) hours, `0x68`/`0x69` its base weight; `0x16` the 7 → D/E count; `0x23` the food menu's cursor; `0x08` bit 1 the clock screen's 24-hour display. The earlier candidates "`0x05E` 0→1 at the child and 1→2 when it got sick" were the age going up at the baby's nap and the next morning's wake.

Death: `0x0B` = F after four sicknesses in one stage (`0x36` counts them, `0x0514`) or after six hours of sickness while awake (`0x38`/`0x39` count the minutes, `0x3A` the hours, `0x08D6`; in the neglect run `0x3A` went 1→6 from 8.1 h to 13.1 h, when the pet died). The sprites of each stage are in `mothra_stage_sprites.txt` in the research folder.

**C toggles a fast mode in this emulation.** On the main screen C toggles `0x08` bit 0 (`0x1DEC`) when `0x14` bit 3 is set, and `0x14` bit 3 is the inverse of input K13 (`0xF42` bit 3, read at `0x0707`). tamalib leaves K10-K13 low, so the ROM sees the pin grounded (most likely a factory test pad) and C works; with the bit set every main-loop pass counts as a second and a minute (`0x0900`, `0x0922`), so the pet's clock, timers and evolution run about an hour per real second until C is pressed again or an event (a sickness, droppings, a call, waking up, evolving) clears it (`0x00E4`). The research scripts never press C on the main screen (menus close on their own after 10 s).

Mothra runs (ROM `build-host/roms/tamamot.bin`; all files under `.superpowers/sdd/2026-11-25-leftovers/research/`, scripts by `care.py` and `evo.py`; logs in `logs/`, the long ones gzipped: `gunzip -k` before `mjoin.py`, `snap.py` or `timeline.py`):

| Name | Save | Script | What it shows |
|---|---|---|---|
| baby | `-` | `w3000 B w1000 A w1000 C w2000 clock ff400 w100 clock ram dump saves/mot_baby.sav` | baby (`0x0B` = 1) at 6.7 min |
| `SWEEP` | `mot_baby.sav` | `sweep.py build-host/roms/tamamot.bin saves/mot_baby.sav 0 9F +5 "w1000 A w1000 B w1500 A w2300 A w2300 A w2300 C w1500"` | each nibble 0x000-0x09F +5 in turn, meter pages compared: only `0x3B` (bar), `0x3D` (happy), `0x3E`/`0x3F` (hunger), `0x56`/`0x57` (weight) and `0x5E`/`0x5F` (age) change them (`0x02`, `0x0A`, `0x0D` are screen state) |
| `BAR` | `mot_baby.sav` | `poke.py … 03B=v`, then the meter | v = 1, 2, 3, 4 → 3, 7, 11, 14 lit columns (¼ … full); 5-F full |
| `MEALS4` | `mot_baby.sav` | (meal `A w1000 A w1000 B w1500 B w12000`, meter) ×4 | `0x3E` 1-4, hunger page 1-4 hearts |
| `SNACK5` | `mot_baby.sav` | snack `A w1000 A w1000 B w1500 A w1500 B w14000` and meter, ×5 (the cursor is remembered, so they alternate meal and snack) | `0x3D` 1, 2, 3 → 1, 2, 3 hearts |
| `CLOCK12` | `mot_baby.sav` | `w1000 set115930 w1000 ram ff60 ram`; `poke.py … 1A=0 1B=3 1C=9 1D=5 1E=B 1F=1` + `ff40`; `w3000 B w1000 ram` + (`A w800 ram`) ×26 from `-` | the carries and menu steps in the Clock row |
| `LIGHT1` | `mot_baby.sav` | `w1000` + `A w900`×7 `B w1500 A w1200 B w3000 C w1000 w3000`, twice (the menu opens on the current state, so the second A picks "on") | `0x08` 0→8, dark screen; 8→0 |
| `SICKFIND` | `mot_baby.sav` | `sick_find2.script` (meals and snacks, then ff with C presses: fast mode, which is how it got sick so early) | `0x05` 0→1, `0x0C` 0→B, `0x36` 0→1 at 1 076 s with the skull; dumped as `mot_sick_baby.sav` |
| `CURE` | `mot_sick_baby.sav` | `w1000 ram` + (`A w900`×6 `B w9000 w12000 ram`) ×2 | dose 1 `0x6A` 1→0, still sick; dose 2 `0x05` 1→0, skull gone |
| `POOP1` | `care4_24h.sav` | `care.py 10.5 10 0900 snack=0 game=1 clean=0` | `0x07` 1, 2, 3, 4 with as many piles |
| `CARE2` | `mot_baby.sav` | `w500 set090000 ram` + `care.py 30 10 0900 meter=6` | 30 h: child at 1.05 h, weight to 99, asleep 20:21-08:04, light 8 at bedtime and back at the wake, age 1→2 at the wake |
| `CARE3` | `mot_baby.sav` | `care3.script` (`care.py 240 10 0900 meter=36`, snacks every block, light only switched off the first night) | 2 → 3 at 50 h → 5 at 144.7 h → A at 145.9 h → F at 224 h (fourth sickness of the stage) |
| `CARE4` | `mot_baby.sav` | `care4.script` (`care.py 240 10 0900 snack=0 game=1 meter=36 dump_h=24 dump_prefix=care4`) | 2 → 4 at 52 h (happy fell to 0: mistakes) → 5 → A at 147 h → F at 218 h; daily saves `care4_<h>h.sav` |
| `CARE5` | `mot_baby.sav` | `care5.script` (`care.py 300 10 0900 snack=0 game=3 meter=36 dump_h=24 dump_prefix=care5`) | no care mistakes in 300 h: 2 → 3 at 57 h, bar 4 by 78 h, 5 (cocoon) at 147.5 h → 7 at 148.5 h; weight stays at the 7's base 60 t (three games a block), so no D; 26 meter visits agree; dark screen at exactly the 977 of 1 801 dumps with `0x08` bit 3 set, also while bit 1 (24-hour display, toggled by a stray press on the clock screen) was set |
| `EVO` | `care4_96h.sav` and the ends of earlier `EVO` runs | `evo.py <save> <name> [37=… 3B=… 0B=… 57=… 16=…]`: sets the awake-time counters 5 min short of the target, then 3 h of `ff600 ram` | teen 4 with mistakes 0/3/F (bar 4) → 5 → 8/9/A; teen 3 (bar 4, mistakes 0) → 7, (bar 1, mistakes 5) → A; 8 → B, A → C (bar 4, no mistakes); 7 at 75 t with `0x16` = 1 → D, `0x16` = 4 → E |

**Not found:** nothing the glance needs.
