<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Looking after the Mothra pet through `look` and `press`

Use this guide when the ball is running Mothra (`status` reports `model: Mothra`). Read the P1 guide first (`tamaorb://guide/p1`) for the tools and the way menus work. Much less is known about Mothra than about the other models: lines marked "checked" were confirmed by running the ROM in the emulator on a PC (docs/pet-ram.md); everything else is a guess to confirm on screen.

## Icons (checked)

`look` names icons by P1's names, which don't match Mothra's glass. From `icons: none`, A steps through them in this order:

| `look` says | On Mothra's glass | What it does |
|---|---|---|
| food | circle | meter (checked) |
| light | leaves | food (checked) |
| game | bow | not identified |
| medicine | coil | not identified |
| bathroom | shouting mouth | not identified (P1 uses it for discipline) |
| meter | two fairies | not identified |
| discipline | sun | light (checked) |
| attention | sparkle | attention (lights by itself) |

## Meter (checked)

A once, then B: age and weight (in tons); A: hunger hearts; A: happy hearts; A: a bar. Titles are Japanese characters; go by position.

## Care

- **The egg (checked).** A new Mothra starts as an egg in its clock-setting mode; the ball sets its clock for you.
- **Food (checked).** A twice, B, then B: one meal adds one hunger heart (four at most). Press C twice afterwards.
- **Everything else** (games, sickness, sleep, droppings, discipline) was not worked out. Read the screen, try one press, look again, and press C until `icons: none` if it is unclear. The ball's glance only shows hunger for Mothra.

## Routine

1. `look`. If `phase` is not `run`, wait.
2. Press C until `icons: none`.
3. Open the meter ("A B") and read hunger ("A"), then C until `icons: none`.
4. Feed when hunger is low. For anything else, stop and tell the owner what the screen shows instead of pressing more buttons.
