<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Looking after the P1 pet with `look` and `press`

`look` shows the 32x16 LCD as text (`#` lit, `.` unlit), then `icons:` (lit icons by name), `buzzer:`, `phase:` and `frame:` (a counter that only tells you the screen changed). `press` takes steps A, B, C, AC and returns the same text. Statements marked "may" or "usually" are from general knowledge, not checked on this ball.

## Buttons

`press` takes a `buttons` argument: a string such as "A A B" or an array ["A","A","B"]. It returns an error while the ball is booting or catching up, so wait and retry.

- **A** moves the selection to the next icon. From the plain pet view (`icons: none`) the first A lights food, and each further A lights the next one, so the selected icon is the single name in `icons:`. After discipline, one more A clears the selection (`icons: none`).
- **B** opens the selected icon. With nothing selected, B shows the clock; press B again to go back (C does nothing there).
- **C** cancels: inside a menu it goes back one level; with only an icon selected it clears the selection. Pressing C until `icons: none` is a safe way back and does not feed or change anything.
- **AC** is not needed. Avoid it.

Sequences are fine when you know the path: "A A A A A A B" opens the meter. Keep the default timing. The game reads about one press per 500 ms, and faster presses get lost. Check `icons:` in the reply before going on: if it isn't what you expected, press C until `icons: none` and start again. During an animation (eating, cleaning, the game) presses may be ignored, so wait for the picture to settle.

## The icons, in order

Checked on this ball, one A at a time from `icons: none`:

1. food: choose a meal or a snack.
2. light: room light on or off.
3. game: a guessing game.
4. medicine
5. bathroom: clean up droppings.
6. meter: read-only status screens.
7. discipline
8. (one more A: `icons: none`)

`attention` is not selectable. It lights by itself when the pet wants you; `buzzer: on` often goes with it.

## What the pet needs

- **Feeding.** Select food (A once from `icons: none`), press B: a menu opens. The first option is usually the meal, and A moves between options, so check which one is marked before going on. With the meal marked, press B, wait for the eating animation to finish, then check the hunger hearts on the meter. C backs out without feeding. A meal fills hunger hearts; a snack mainly cheers the pet up and adds weight, so prefer meals.
- **Droppings** (a small pile beside the pet): bathroom, then B. A cleaning animation plays; when there is nothing to clean it still plays and does no harm.
- **Sick** (a skull-like shape on screen): medicine. It may take more than one dose.
- **Asleep** (eyes closed, picture mostly still or dark): turn the light off, then back on when it wakes. `icons:` does not report the light state; judge by the picture.
- **Low happy hearts:** game. Select it (A three times) and press B. A short intro plays (hearts, the pet walking in), then the pet stands in the middle and waits for your guess: A for one side, B for the other. It moves, and the rounds repeat a few times before the game ends by itself. On this ball the result screen was not identified, so check the happy hearts on the meter afterwards to see whether it helped. A win usually adds a happy heart.
- **Attention lit.** Usual causes: low hunger hearts, low happy hearts, sickness, droppings, the light on at bedtime, or a "fake" call that wants discipline. Check the screen and the meter first and fix the real need. Discipline is only for a call with no real need behind it. Disciplining a pet that was calling for food, medicine, the toilet or sleep may do harm. If you are sure, select discipline (A seven times), press B and look. Press C if unsure.

## Meter screens

Select meter (A six times), press B. This is read-only. It opens on age and weight; each A shows the next screen: discipline, then hunger, then happy (so three A presses reach happy). C leaves. Read the title word at the top of each screen if there is one.

- **Age and weight:** a pet sprite with a small number, and a scale sprite with another. The digits are tiny; read them carefully.
- **Discipline:** a wide bar outline. An empty outline means low discipline.
- **Hunger and happy:** four hearts in a row, read left to right. A full heart is solid; an empty one is an outline:

```
.##.##.   .##.##.
#.#####   #..#..#
#.#####   #.....#
#######   #.....#
.#####.   .#...#.
..###..   ..#.#..
...#...   ...#...
 full      empty
```

(Shapes copied from this ball's screen.)

Count the solid ones, 0 to 4. Two or more is fine; feed or play when a row drops to one or none.

## A routine

1. `look`. If `phase` is not `run` (boot or catch-up), wait and look again.
2. Open the meter ("A A A A A A B"), read the hunger and happy hearts ("A A" then "A"), and leave with C.
3. Fix one need at a time: food, bathroom, medicine, light, game. After each, check again.
4. Press C until `icons: none`.

Hearts drop over real time, so check every few tens of minutes, not constantly. A baby pet is a small round blob; it grows into other shapes over days. If a screen looks unfamiliar, press C a few times, look, and describe it to the owner instead of guessing.
