# Icon references

TamaOrb draws its own LCD icons. This folder records what we looked at, and under which terms, so future icon work can start from here.

**Only material whose license allows redistribution is stored in this folder.** Everything else is linked.

## Icon meanings (ROM order)

| # | Row | Function | Real P1 artwork | Real P2 artwork |
|---|---|---|---|---|
| 0 | top | Feed | knife and fork | same |
| 1 | top | Light | light bulb with rays | same |
| 2 | top | Play | bat and ball | same |
| 3 | top | Medicine | syringe | **bottle and spoon** (the only P2 difference) |
| 4 | bottom | Bathroom | rubber duck | same |
| 5 | bottom | Status | scale / gauge | same |
| 6 | bottom | Discipline | open shouting mouth with zigzag | same |
| 7 | bottom | Attention | two faces | same |

### Angel and Mothra (bit = TamaOrb's icon number)

Their meanings come from running the ROMs on the PC (`docs/pet-ram.md`). What each glass segment depicts was learned by looking at MAME's `tamaang.svg` and `tamamot.svg` (inside the owner's ROM zips; not stored, not traced). TamaOrb draws Angel with its P1 pictures in Angel's order, and Mothra with its own art (`components/ui/ui_icons.c`). Angel needs no art of its own: rendering `tamaang.svg`'s icon shapes (2026-11-25) showed the same eight subjects as P1's glass (knife and fork, light bulb, bat and ball, syringe, rubber duck, scale, shouting mouth, two faces), so the owner kept P1's pictures.

| Bit | Angel: function (glass picture) | Mothra: function (glass picture) |
|---|---|---|
| 0 | Status (scale) | Status (a plain circle) |
| 1 | Feed (knife and fork) | Feed (leaves) |
| 2 | Play (bat and ball) | not identified (a bow) |
| 3 | Bathroom (rubber duck) | not identified (a coil) |
| 4 | Discipline (shouting mouth) | not identified (shouting mouth) |
| 5 | Medicine (syringe) | not identified (two fairies) |
| 6 | Light (light bulb) | Light (a sun) |
| 7 | Attention (two faces) | Attention (a sparkle) |

Mothra's icons sit on segment/COM pairs tamalib doesn't map; TamaOrb reads them from display memory (`tc_model_icon_pins`).

The real icons are LCD segments on the glass, not printing on the shell. Each is about 4–5 matrix pixels wide, and they sit about 2 pixel rows above and below the 32×16 matrix.

## Sources

| Source | What | License | Stored here? |
|---|---|---|---|
| [agg23/fpga-tamagotchi](https://github.com/agg23/fpga-tamagotchi) `assets/png/spritesheet.png` @ `3ed61f6` (2024-05-19) | All 8 P1 icons recreated as 400×50 RGBA (8 × 50×50) | **CC0** ("All 'sprite' activity icons and backgrounds", README § Licensing) | **Yes:** `agg23-spritesheet-cc0.png` (SHA-1 `2d03b884148c1ca8e066371c22219a1e5b1677eb`) |
| [Round-Pi/flipperzero-tamagotch-p1](https://github.com/Round-Pi/flipperzero-tamagotch-p1) `icons/icon_0..7.png` @ `9de12dc` (2023-02-07) | All 8 icons at 14×14, 1-bit. Shows the icons stay recognisable at very low resolution | **No license** | No (link only) |
| [jcrona/tamatool](https://github.com/jcrona/tamatool) `res/p1-icons.png`, `res/p2-icons.png` @ `6941aab` (2025-03-06) | 64×64 white-on-transparent icons; the P2 sheet differs only in Medicine | GPL-2.0+ repo, but scanned from Bandai hardware | No (link only) |
| MAME `tama.svg` (inside the MAME ROM zips `tama.zip` / `tamag2.zip`, not in MAME's source tree) | Vector map of the whole LCD glass: matrix plus icon segments. MAME uses the same file for P1 and P2 | Derived from Bandai hardware. MAME keeps it out of its repository | No (the owner's local `roms/` only, git-ignored) |
| MAME `tamaang.svg`, `tamamot.svg` (inside the owner's `tamaang.zip` / `tamamot.zip`) | Vector maps of the Angel and Mothra glass, icon segments included | Derived from Bandai hardware. MAME keeps them out of its repository | No (looked at to learn what each icon depicts; nothing traced) |

## Notes for the polish milestone

- Our first icons are 12×12 cells drawn at 2 device px per cell, which looks rough. Redraw them at **24×24, 1 device px per cell**, the same on-screen size at double the detail.
- A test that shrank the CC0 agg23 sheet to 24×24 (box filter, alpha threshold ≈110/255) gave clearly recognisable icons with only light cleanup needed. That's a useful benchmark for our own drawings.
- If we ever decide to ship agg23's art instead of our own, CC0 allows it. Credit agg23 in the README, and keep in mind the designs themselves are Bandai's.
