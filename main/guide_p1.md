<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Looking after the P1 pet with `look` and `press`

`look` shows the 32x16 LCD as text (`#` lit, `.` unlit), then `icons:` (lit icons by name), `buzzer:` and `phase:`. `press` takes steps A, B, C, AC and returns the same text. Read the screen after every press. Statements marked "may" or "usually" are from general knowledge, not checked on this ball.

## Buttons

`press` takes a `buttons` argument: a string such as "A A B" or an array ["A","A","B"]. It returns an error while the ball is booting or catching up, so wait and retry.

- **A** moves the selection to the next icon. From the plain pet view (`icons: none`) the first A lights the first icon, food. Each further A lights the next one, so the selected icon is the single name shown in `icons:`. A lit name there means "selected" unless it is `attention`, which lights by itself.
- **B** opens the selected icon.
- **C** cancels. Inside a submenu or screen it usually goes back one level; with only an icon selected it usually clears the selection. Pressing C until `icons: none` and the pet is shown is a safe way back, and it does not feed or change anything.
- **AC** is not needed. Avoid it.

Send one press per call and `look` after every single press. If the screen does not match what you expect, stop and press C.

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

- **Feeding, step by step.** Press A until `icons: food` (look to confirm). Press B: a food menu opens; look and read it. The first option is usually the meal, and A moves between options, so check which one the screen shows as selected before going on. When the meal is selected, press B to feed and look to see the eating animation and the hunger hearts change. C backs out without feeding. If the menu does not look like two choices, press C and stop. A meal mainly fills hunger hearts; a snack mainly cheers the pet up and usually adds weight, so prefer meals.
- **Droppings** (a small pile beside the pet): bathroom. Ignored, they tend to make the pet sick.
- **Sick** (a skull-like shape on screen): medicine. It may take more than one dose.
- **Asleep** (picture mostly still or empty): turn the light off, then back on when it wakes. Judge by the picture; `icons:` does not report the light state.
- **Low happy hearts:** game. Pick left or right with A, confirm with B, and read the result on screen.
- **Attention lit.** Usual causes, all hedged: low hunger hearts, low happy hearts, sickness, droppings, the light on at bedtime, or a "fake" call that wants discipline. Check the screen and the meter first and fix the real need. Discipline is only for a call with no real need behind it. Disciplining a pet that was calling for food, medicine, the toilet or sleep may do harm and lower its mood. If you are sure: press A until `icons: discipline` (it is presumably after meter, which I did not step past), press B, and look at the result. Press C if unsure.

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
