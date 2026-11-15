<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Looking after the P1 pet with `look` and `press`

`look` shows the 32x16 LCD as text (`#` lit, `.` unlit), then `icons:` (lit icons by name), `buzzer:` and `phase:`. `press` takes steps A, B, C, AC and returns the same text. Read the screen after every press. Statements marked "may" or "usually" are from general knowledge, not checked on this ball.

## Buttons

- **A** selects the next icon. From the plain pet view the first A selects food; the selected icon is the one named in `icons:`.
- **B** opens the selected icon.
- **C** cancels or goes back. Press C until `icons: none` to reach the base state.
- **AC** is not needed. Avoid it.

Press one step at a time and look in between; a stray B can feed, play or medicate by accident.

## The eight icons, in order

1. food: choose a meal or a snack.
2. light: room light on or off.
3. game: a guessing game (left or right).
4. medicine
5. bathroom: clean up droppings.
6. meter: read-only status screens.
7. discipline
8. attention: not selectable. It lights by itself when the pet wants you; `buzzer: on` often goes with it.

I confirmed that A steps food, light, game, medicine, bathroom, meter in that order. Discipline is presumably next.

## What the pet needs

- **Hungry or unhappy:** food. A meal mainly fills the hunger hearts, a snack mainly cheers the pet up, and snacks usually add weight, so prefer meals and use snacks sparingly.
- **Droppings** (a small pile beside the pet): bathroom. Ignored, they tend to make the pet sick.
- **Sick** (a skull-like shape on screen): medicine. It may take more than one dose.
- **Asleep** (picture mostly still or empty): turn the light off, then back on when it wakes. Check with `look`; the lit icon list will not tell you the light state, only the picture.
- **Low happy hearts:** game. Pick left or right with A, confirm with B, and read the result on screen.
- **Attention lit, no obvious need:** the pet may be asking for discipline. Check the meter screens first; use discipline only when nothing else explains the call.

## Meter screens

Select meter (A until `icons: meter`), press B. This is read-only. A pages through the screens and wraps around; C leaves. On this ball the order was: age and weight, discipline, hunger, happy. Other versions may differ, so read the title word at the top of each screen.

- **Age and weight:** a pet sprite with a small number, and a scale sprite with another. The digits are tiny; read them carefully.
- **Discipline:** the word spelled out, then a wide bar outline. An empty outline means low discipline.
- **Hunger and Happy:** the word at the top and four hearts below. A full heart is solid (`#` inside), an empty one is an outline (`.` inside). Count the solid ones, 0 to 4.

## A routine

1. `look`. If `phase` is not `run` (boot or catch-up), wait and look again.
2. Open the meter and read hunger and happy hearts. Leave with C.
3. Fix one need at a time: food, bathroom, medicine, light, game. After each, `look` and compare.
4. Press C until `icons: none`.

Hearts drop over real time, so check every few tens of minutes, not constantly. If a screen looks unfamiliar, press C a few times, look, and describe it to the owner instead of guessing.
