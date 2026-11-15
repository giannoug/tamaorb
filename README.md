# TamaOrb

Authentic first-generation Tamagotchi emulation for round ESP32-S3 devices. TamaOrb runs the original **P1** and **P2** ROMs on [tamalib](https://github.com/jcrona/tamalib), around the clock, on the SpotPear 1.28" AI Voice Robot Ball (legs, touch and battery).

**No ROMs are included.** Use your own dumps.

## What it does

- **Runs the real ROM in real time, all the time.** On USB power the screen stays on. On battery it turns off after 30 s, but the pet keeps living.
- **Saves crash-safely:** 3 s after you press a button, and every 5 minutes. The power button can cut power at any moment without losing the pet.
- **Catches up after power-offs.** On boot it gets the time over Wi-Fi (NTP) and replays the missed time at about 25x speed, with a progress screen. Without Wi-Fi it resumes where it was saved.
- **Each ROM keeps its own pet.** A pet on the shelf keeps aging and is caught up when you switch back.
- **Handles low battery:** it saves, shows a warning, and deep-sleeps until USB is back.

## Supported ROMs

| Model | Size | SHA-1 |
|---|---|---|
| P1 | 12,288 bytes | `4b4979cf92dc9d2fb6d7295a38f209f3da144f72` |
| P2 | 12,288 bytes | `09e5101b37636a314fc599d5d69b4846721b3c88` |

These are the MAME `tama` / `tamag2` sets; MAME zips work directly with the tool below.

## Controls

| Action | How |
|---|---|
| A / B / C | Touch the left / middle / right third of the screen |
| A + C | Touch A and slide to C (the touch chip can't tell two side-by-side fingers apart) |
| Wake the screen | Tap anywhere. The first tap only wakes it, it never presses a button |
| Screen on/off | Short press BOOT |
| Switch pet / ROM | Hold BOOT for 2 s: A up, C down, B pick, BOOT cancel |

## Build and flash

Needs ESP-IDF **v6.1.1**.

```bash
git submodule update --init
. ~/esp/esp-idf-v6.1.1/export.sh
idf.py set-target esp32s3 build
idf.py -p /dev/ttyACM0 flash
```

Sound is muted by default. Set `TAMAORB_VOLUME` (0–100) in `idf.py menuconfig` → *TamaOrb board*.

## ROMs and Wi-Fi over USB

`tools/tamaorb-rom.py` needs Python 3 and `pip install -r tools/requirements.txt`. It talks to the ball over the flashing cable:

```bash
tools/tamaorb-rom.py push roms/tama.zip roms/tamag2.zip   # .bin dumps or MAME zips; only P1/P2 are sent
tools/tamaorb-rom.py list
tools/tamaorb-rom.py remove tamag2.bin
tools/tamaorb-rom.py wifi "My Network"                    # asks for the password; the ball reboots
tools/tamaorb-rom.py status
tools/tamaorb-rom.py get                                  # all settings as key=value
tools/tamaorb-rom.py set brightness=30 palette=night      # all or nothing; applied live, kept across power cuts
```

ROMs go to `/tamaorb/roms/` on the microSD card. Without a card, a pushed ROM becomes the active one directly. The active ROM is also kept in internal flash, so the pet survives a missing or failed card.

## Tests

```bash
test/host/run.sh                         # portable core on Linux (ROM tests use roms/*.zip if present)
pip install -r tools/requirements.txt && pytest tools
```

## Project layout and docs

- `components/tama_core`, `ui`, `persist`, `hostlink`: portable code, host-tested.
- `components/board/spotpear_ball_128`: the only hardware-specific code. Hardware notes are in `docs/hardware/`.
- `docs/superpowers/specs/`: the design.
- `docs/adr/`: decisions.
- `docs/upstream/`: notes for tamalib.

## Credits and license

TamaOrb is GPL-3.0-or-later (`LICENSE`). It builds on:
- [tamalib](https://github.com/jcrona/tamalib) by Jean-Christophe Rona, GPL-2.0-or-later;
- [font8x8](https://github.com/dhepper/font8x8) by Daniel Hepper, public domain.

The icons are TamaOrb's own pixel art. The references we looked at are recorded in `docs/references/icons/`. Tamagotchi is a trademark of Bandai; this project is not affiliated with Bandai.
