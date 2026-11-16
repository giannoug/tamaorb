<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Looking after the P2 pet through `look` and `press`

Use this guide when the ball is running P2 (`status` reports `model: P2`). Read the P1 guide first (`tamaorb://guide/p1`): the buttons, icons and meter work the same way. Lines marked "checked" were confirmed by replaying a P2 save from this ball in the emulator on a PC; the rest is general knowledge, so confirm it on screen.

## What is the same as P1 (checked)

- The tools: `look` shows the LCD as text (`#` lit, `.` unlit), lit icons by name, the buzzer and the phase. `press` takes A, B, C or AC. Keep the default press timing: the ROM reads about one press per 500 ms.
- The icons, in order: food, light, game, medicine, bathroom, meter, discipline; one more A clears the selection. `attention` lights by itself and is not selectable.
- The meter opens on age and weight; A then shows discipline, hunger and happy, and wraps around. The hearts look the same: solid is full, outline is empty, four per row.

## What is different

- **Japanese text (checked).** Menu items and meter titles are in Japanese characters, which don't read as words in `look`. Go by position instead: the meter order above, and the food menu below.
- **The egg (checked).** A new P2 pet starts as an egg, and the buttons do nothing until it hatches; A does not light any icon. Wait and look again a few minutes later.
- **Food (checked).** Select food (A once from `icons: none`) and press B: a two-line menu opens with the arrow on the first line, the meal. Press B to feed: after the eating animation the menu comes back, and each B there feeds another meal. One meal adds one hunger heart. Then press C **twice**: the first C leaves `food` still selected, and counting A presses from there lands one icon too far.
- **Pets and growth.** P2 has its own characters and timings, so sprites won't match the P1 guide. Rely on icons, hearts and what the owner tells you.
- **Games.** Not checked on P2. Read the screen, try one press, look again, and press C until `icons: none` if it is unclear.
- **AC** may toggle the sound on some versions. Avoid it.

## Routine

1. `look`. If `phase` is not `run`, wait.
2. Press C until `icons: none`.
3. Open the meter ("A A A A A A B"), read hunger ("A A") and happy ("A"), then C until `icons: none`.
4. Act on one need at a time: food, bathroom, medicine, light, game. After each, check the hearts again.
5. If something is unexpected and you cannot tell what it means, stop and tell the owner what the screen shows instead of pressing more buttons.
