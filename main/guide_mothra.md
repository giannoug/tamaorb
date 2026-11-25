<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Looking after the Mothra pet through `look` and `press`

Use this guide when the ball is running Mothra (`status` reports `model: Mothra`). Read the P1 guide first (`tamaorb://guide/p1`) for the tools and the way menus work. Lines marked "checked" were confirmed by running the ROM in the emulator on a PC (docs/pet-ram.md); everything else is a guess to confirm on screen.

## Icons (checked)

`look` names icons by P1's names, which don't match Mothra's glass. From `icons: none`, A steps through them in this order:

| A presses | `look` says | On Mothra's glass | What it does |
|---|---|---|---|
| 1 | food | circle | meter |
| 2 | light | leaves | food |
| 3 | game | bow | game |
| 4 | medicine | coil | cleans the droppings |
| 5 | bathroom | shouting mouth | scold |
| 6 | meter | two fairies | medicine |
| 7 | discipline | sun | light |
| — | attention | sparkle | attention (lights by itself) |

## Meter (checked)

A once, then B: age (days) and weight (tons); A: hunger hearts; A: happy hearts; A: the justice bar (quarters). Titles are Japanese characters; go by position.

## Care (checked)

- **The egg.** A new Mothra starts as an egg in its clock-setting mode; the ball sets its clock for you. It hatches about 5½ minutes later.
- **Food.** A twice, B: a two-line menu (meal, snack). The arrow starts on whichever was chosen last, so look before pressing B. A meal adds a hunger heart (refused at four); a snack adds a happy heart and weight. Press C twice afterwards.
- **Game.** A three times, B. About half the games won add a happy heart; each costs a ton.
- **Droppings.** Piles on screen; A four times, B cleans them.
- **Scold.** A five times, B, only while the pet makes its misbehaving call: it fills the justice bar a quarter.
- **Sickness.** A skull while awake. Medicine is A six times, B; it can take two doses or more (the baby needs two). Untreated sickness kills.
- **Light and sleep.** It sleeps at night (the hours depend on its stage) and naps once as a baby. Switch the light off when it sleeps (A seven times, B, pick off); the ROM turns it back on at wake-up.

## Routine

1. `look`. If `phase` is not `run`, wait.
2. Press C until `icons: none`.
3. Open the meter ("A B"), read hunger and happy ("A", "A"), then C until `icons: none`.
4. Feed when hunger is low, play or give a snack when happy is low, clean droppings, give medicine when the skull shows. For anything unclear, stop and tell the owner what the screen shows instead of pressing more buttons.
