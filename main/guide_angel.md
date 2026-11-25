<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Looking after the Angel pet through `look` and `press`

Use this guide when the ball is running Angel (`status` reports `model: Angel`). Read the P1 guide first (`tamaorb://guide/p1`) for the tools and the way menus work. Lines marked "checked" were confirmed by running the Angel ROM in the emulator on a PC (docs/pet-ram.md); the rest is general knowledge, so confirm it on screen.

## Icons (checked)

`look` names icons by P1's names, which don't match Angel's glass. From `icons: none`, A steps through them in this order:

| `look` says | On Angel |
|---|---|
| food | meter |
| light | food |
| game | game |
| medicine | bathroom |
| bathroom | discipline (praise/scold) |
| meter | medicine |
| discipline | light |
| attention | attention (lights by itself, not selectable) |

So: meter is A once, food A twice, game three times, bathroom four, discipline five, medicine six, light seven. One more A clears the selection.

## Meter (checked)

A once, then B: age (days) and weight (a heart after the number); A: a bar (Angel's stand-in for discipline); A: hunger hearts; A: happy hearts; A wraps around. Titles are Japanese characters; go by position. Hearts: solid is full, outline empty, four per row.

## Care (checked unless marked)

- **The egg.** A new Angel starts as an egg in its clock-setting mode; the ball sets its clock for you. It hatches about half an hour later.
- **Food.** A twice, B: a two-line menu, arrow on the meal. B feeds a meal (one hunger heart, weight +0); A then B gives the snack (weight +2, no heart). Press C twice afterwards.
- **Game.** A three times, B. A win adds to happy.
- **Light.** A seven times, B, A, B switches it off. Don't switch the light off by day: in a test that made the pet sick at once.
- **Sleep.** It falls asleep at 20:00 (the screen shows it asleep).
- **Sickness.** A skull on screen; medicine is A six times, B. Not checked how many doses it takes.
- **Droppings (checked).** They only come once happy is high (and after the baby stage), a pile at a time. The bathroom, A four times, B, cleans them.

## Routine

1. `look`. If `phase` is not `run`, wait.
2. Press C until `icons: none`.
3. Open the meter ("A B"), read hunger ("A A") and happy ("A"), then C until `icons: none`.
4. Act on one need at a time. After each, check the hearts again.
5. If something is unexpected and you cannot tell what it means, stop and tell the owner what the screen shows instead of pressing more buttons.
