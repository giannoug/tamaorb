# TamaOrb: Design

Date: 2026-11-15
Status: draft, awaiting approval

TamaOrb (`tamaorb`) is open-source firmware that turns a small round ESP32-S3 gadget into a first-generation Tamagotchi. It runs the original ROMs on an existing emulator core rather than recreating the game.

Throughout this document, **(decided)** marks a choice the project owner made, **(measured)** marks something verified on the real device, and **(builder's call)** marks a choice made during design that the owner hasn't explicitly confirmed. Builder's calls may be revisited if implementation shows a better option.

---

## 1. Intent

- **What it is:** an authentic Tamagotchi P1/P2 that lives on a desk. It mostly runs on USB power and occasionally on battery. **(decided)**
- **The pet keeps living.** The emulator runs in real time around the clock. Turning the screen off is purely cosmetic. **(decided)**
- **When power is lost,** the pet catches up on the missed time on the next boot, using Wi-Fi/NTP for the real time. It doesn't just freeze. **(decided)**
- **Authentic:** original ROMs, original pixels (scaled with hard edges, no smoothing), original beeps, original button semantics.

### Goals (first version)

1. Emulate Tamagotchi **P1 and P2** ROMs with tamalib. **(decided)**
2. Round-screen UI: the 32×16 LCD at 6×, 8 status icons, three invisible touch zones A/B/C. **(decided: layout A)**
3. Original sound through the ES8311 codec and speaker.
4. Persistent state that survives a hard power cut at any moment.
5. Wi-Fi stays connected. NTP provides real time, which is used to catch up the pet after an outage. **(decided)**
6. Screen timeout without pausing emulation.
7. Battery monitoring and a safe low-battery behaviour.
8. ROMs loaded from the SD card, recognised by SHA-1. **(decided)**
9. A host tool that moves ROMs from the PC to the SD card over the USB cable. **(decided: wanted)**
10. Architecture that makes future hardware ports straightforward.

### Non-goals (first version)

- Web UI, HTTP API, MCP server. These are planned next, so the architecture must leave room for them (§8), but none ship in v1. **(decided)**
- Angel, Mothra and other E0C6S48 models. They come later, mainly needing new icon sets and a tap-sensor mapping. **(decided)**
- Speeding up tamalib's catch-up. This is tracked separately (§12). v1 ships with the measured ~29× catch-up and a progress screen. **(decided)**
- Writing the in-game clock from NTP (each ROM keeps its clock in different RAM addresses).
- OTA updates (the partition layout should allow them later).
- Microphone, RGB LED, voice features.

### Anti-goals (would make it a failure even if it "works")

- Shipping, embedding or committing any ROM, or Bandai artwork (including TamaTool's scanned icon PNGs).
- Smoothed or blurred pixels.
- Stopping, slowing or desynchronising the emulator to save power or while the screen is off.
- Losing the pet, or corrupting saves, because the power button cut power mid-write.
- A touch on a dark screen that presses a game button blindly.

---

## 2. Constraints

- **License:** tamalib is GPL-2.0-or-later and ESP-IDF is Apache-2.0. Apache-2.0 can't be combined with GPL-2.0-only code, so **TamaOrb is GPL-3.0-or-later**. **(builder's call; owner agreed with the architecture)**
- **No ROMs in the repo.** The `.gitignore` already excludes `roms/`, `*.bin` (except test fixtures) and `hw-backup/`.
- **Offline-tolerant:** Wi-Fi is optional at runtime. With no Wi-Fi or NTP, everything works except catch-up (§6.4).
- **Framework:** ESP-IDF **v6.1.1** (xiaozhi-esp32 now requires IDF ≥ 6.0.1, and 6.1 is current stable). C, no Arduino, no LVGL. **(builder's call)**
- **Dependencies:** only ESP-IDF, its official components (`esp_lcd`, `esp_codec_dev` or a direct ES8311 driver, `sdmmc`/`fatfs`, `esp_wifi`, `esp_netif_sntp`) and tamalib. Nothing else without a reason.

---

## 3. Hardware: SpotPear AI Voice Robot Ball (legs, touch, battery)

Sources: xiaozhi-esp32 `main/boards/spotpear/sp-esp32-s3-1.28-box`, RealDeco/xiaozhi-esphome `devices/Spotpear Balls/Ball_v2.yaml`, socquique/TamaPoke PR #44 (tested on this exact device), and a throwaway probe run on the owner's unit on 2026-11-15.

| Item | Detail | Confidence |
|---|---|---|
| MCU | ESP32-S3 (QFN56) rev 0.2, 240 MHz dual core, native USB-Serial/JTAG, 40 MHz crystal | measured |
| Flash / PSRAM | 16 MB Winbond (quad), 8 MB embedded PSRAM | measured |
| Display | GC9A01A 240×240 round, SPI: SCLK 4, MOSI 2, CS 5, DC 47, RST 38; BGR order, colour inversion on, mirror X; backlight GPIO42 (PWM, **active low**) | 2 sources agree; TamaPoke-tested at 40/80 MHz SPI |
| Display init quirks | xiaozhi sends extra registers after init: `0x62`, `0x63`, `0x36=0x48`, `0xC3=0x1F`, `0xC4=0x1F` | xiaozhi source |
| Touch | CST816D at I2C 0x15 on its own bus: SDA 11, SCL 7, RST 6, INT 12. Chip ID reg `0xA7=0xB6`, FW `0xA9=0x01` | measured |
| Two-point touch | **Works.** Reg `0x02` = finger count (0–2). Point 1: regs `0x03–0x06`. Point 2: regs `0x09–0x0C` (same layout; touch ID in the upper nibble of `0x0B`). Event flags in the top 2 bits of the X-high byte (0 = down, 1 = up, 2 = contact). Unused point-2 regs read `0xFF`. | measured |
| Audio | ES8311 at I2C 0x18 (bus: SDA 15, SCL 14). I2S: MCLK 16, BCLK 9, WS 45, DOUT 8 (to codec), DIN 10. Speaker amp enable GPIO46 | 2 sources agree; TamaPoke-tested ("loud; lowest volume comfortable") |
| Other I2C devices | **None.** No RTC, no IMU, no PMU | measured (scanned both buses) |
| microSD | SDMMC 1-bit: CLK 17, CMD 18, D0 21 | TamaPoke-tested; probe saw pull-ups on these pins |
| Battery voltage | GPIO1 / ADC1_CH0, 12 dB attenuation, divider ×2. Full battery ≈ 4.17 V (raw ≈ 2557, ≈ 2084 mV at the pin). xiaozhi's raw table: 1980=0%, 2081=20%, 2163=40%, 2250=60%, 2340=80%, 2480=100% | measured (full point only) |
| GPIO41 | Reads 1 with USB connected, 0 on battery (even with a full battery). Behaves like **USB present**; whether it also signals "charging" is unverified | measured |
| BOOT button | GPIO0, active low (strapping pin) | measured |
| GPIO3 | **Not a power latch.** Driving it low on battery for 5 s did not power the board off. xiaozhi/TamaPoke treat it as a latch, untested. Drive it HIGH at boot anyway (harmless). | measured |
| Power button | **A hard power switch.** The firmware can't detect it, it cuts power even on USB, and plugging in USB does **not** turn the board on | measured |
| Firmware power-off | **Impossible.** The firmware can only deep-sleep | measured |
| RGB LED | WS2812 on GPIO48 (unused in v1) | sources |
| Unconnected pins | 13, 39, 40, 43, 44 float | measured |

The full flash as found on 2026-11-15 (the owner's TamaPoke firmware, not stock xiaozhi) is backed up at `hw-backup/device-flash-2026-11-15-full-16MB.bin` (git-ignored; it may contain Wi-Fi credentials).

The detailed hardware notes above should live in `docs/hardware/spotpear-ball-128.md` in the repo.

---

## 4. Emulation core

- **tamalib** (github.com/jcrona/tamalib, GPL-2.0+, last commit 2025-04) is used **unmodified, as a git submodule**. Our `hal_types.h` sits beside it, as tamalib's `#include "../hal_types.h"` expects. **(builder's call)**
- The ESP32 ports (ArduinoGotchi → RBEGamer → EggSP32, anabolyc) were evaluated and rejected as code sources. They use a heavily modified Uno-oriented tamalib, are P1-only with the ROM compiled in, have no proper sleep, and some redistribute the ROM or lack a license.
- **Integration:** use `tamalib_step()` from our own loop, not `tamalib_mainloop()`. The HAL provides `get_timestamp`/`sleep_until` (µs, from `esp_timer`), `set_lcd_matrix`/`set_lcd_icon` (into a shared frame state), and `set_frequency`/`play_frequency` (into the shared tone state). `handler` and `update_screen` aren't used with `tamalib_step()`.
- **Speed control:** `tamalib_set_speed(1)` for real time, `tamalib_set_speed(0)` for catch-up.
- **Known upstream issues** (report upstream; don't patch the submodule in v1):
  - In `cpu.c` `cpu_step()`, the local `i` is read uninitialised when the CPU is halted (GCC `-Wmaybe-uninitialized` at line ~2002). The build must tolerate the warning (`-Wno-error=maybe-uninitialized` on the tamalib sources only).
  - Calling `tamalib_init()` a second time in the same boot leaves the CPU stuck halted. **Therefore switching ROMs reboots the device.**
- **Measured performance** (ESP32-S3 at 240 MHz, `-O2`, unmodified tamalib):
  - P1 in real time uses about **3.4% of one core**.
  - P1 flat out (speed 0) runs at about **29× real time** (≈0.17 M steps/s). An 8-hour outage therefore takes ≈16 minutes to catch up.

---

## 5. User interface

### 5.1 Screen layout (decided: option A from the mockups)

- 240×240 round panel.
- The 32×16 LCD is drawn at **6× = 192×96**, at x 24–216, y 72–168.
- Each LCD pixel is a 5×5 square with a 1 px gap, giving a visible pixel grid. Unlit pixels are drawn in a faint "ghost" shade, lit pixels dark, on a greenish-grey LCD background. No smoothing anywhere.
- **Icons:** 4 above (centred at about y 48) and 4 below (about y 192), at x ≈ 57, 97, 143, 183. Unlit icons are drawn in the ghost colour.
- The icon bitmaps are **our own original pixel art**. P1 and P2 share **one set**; the only per-model difference is **Medicine**: a syringe on P1, a bottle and spoon on P2. This was confirmed against TamaTool's `p1-icons.png` and `p2-icons.png`.
- The icons depict the real concepts, in ROM order: cutlery, bulb, bat and ball, medicine (top row); duck, scale/gauge, scolding mouth, two faces (bottom row). That way players recognise them.
- **Reference only, never traced:** MAME's `tama.svg` (in the owner's ROM zips) and TamaTool's icon PNGs. Never copy Bandai, MAME or TamaTool artwork.
- **Keep our own art for now.** **(decided)** The references, their licenses and a CC0 set we could adopt later are recorded in `docs/references/icons/`.
- **Polish milestone:** redraw at 24×24 cells, 1 device px each, instead of the current rough 12×12 at 2 px.
- **Visual reference:** the design canvas "TamaOrb Display" (https://claude.ai/artifact/TJ6TCPiuNZTi4x8PnRNLAk, private to the owner). It holds:
  - the pet screen (interactive, 2× scale);
  - three LCD palettes: Classic (default), Neutral and Night;
  - original P1 and P2 icon sets as 12×12 bitmaps;
  - the catch-up, low-battery, ROM-picker and message screens.
- Copy palette values and icon bitmaps from the canvas. Further changes happen there first, and the owner reviews them.
- **Classic palette:** LCD background `#B6C19E`, ink `#1C2518`, ghost = background mixed 11% toward ink.
- **Icons** are 12×12 bitmaps drawn at 2 device px per cell (24×24 px).
- **Text screens** use a pixel font. The canvas uses Silkscreen (OFL) as a stand-in; the device needs a small bitmap font (builder's call).
- The whole background is LCD-coloured. Faint dashed dividers at x = 80 and x = 160, plus small "A B C" letters near the bottom edge, are the only touch hints.

### 5.2 Touch → buttons

- The screen is split into three vertical thirds: x < 80 = **A** (left), 80–159 = **B** (middle), ≥ 160 = **C** (right). These map to tamalib `BTN_LEFT`, `BTN_MIDDLE`, `BTN_RIGHT`.
- **Press semantics:** a button stays pressed for as long as a finger is down. A single finger keeps the button it started on while it moves, so sliding never presses something else by accident. **(builder's call)**
- **A+C:** touch A and slide to C (or C to A); both are held until the finger lifts. Measured on hardware: the CST816D averages two fingers at the same height into one point, so a real two-finger A+C isn't reliable. When the chip does separate two fingers (diagonal placements), each point presses its own zone. **(builder's call, owner confirmed the slide on hardware)**
- **Feedback:** the held zone gets a faint tint.
- Touch coordinates may need mirroring to match the display orientation. Verify on hardware.

### 5.3 Screen timeout

- The backlight turns off after a timeout (default 30 s, a compile-time setting in v1). **The emulator never pauses.**
- While the screen is dark, the **first touch only wakes it** and is never forwarded to the game.
- When the ROM starts beeping (tamalib turns sound on), the screen wakes automatically. **(builder's call)**
- While the screen is dark, the UI stops rendering, but the frame state keeps updating.

### 5.4 BOOT button (GPIO0)

- **Short press:** toggle the screen on/off.
- **Long press (≥ 2 s):** open the ROM picker (§7.3). **(builder's call)**
- On future models with a tap sensor (Angel), BOOT becomes that sensor (tamalib `BTN_TAP`). This is a v2 concern.

### 5.5 Other screens (simple, text-based; a small built-in bitmap font)

- **Catch-up:** "Catching up…" with a progress bar and the remaining real-time span.
- **Low battery:** a warning, then sleep (§9).
- **ROM picker:** list of valid ROMs on the SD card, chosen with the A/B/C zones.
- **Messages:** "No ROM on SD card", "Unknown ROM (SHA-1 …)", "SD card missing — using cached ROM".

### 5.6 Sound

- Original 1-bit beeps. Generate a square wave at tamalib's requested frequency (given in dHz, so divide by 10 for Hz) while `play_frequency(true)`. Output it over I2S to the ES8311.
- Buzzer changes go straight from the emulator task to the audio driver. Polling them missed the ROM's ~54 ms key clicks (measured).
- **The amp (GPIO46) stays on.** Switching it on per beep swallowed the short key clicks (measured). The owner hears a faint hum from the always-on amp; to revisit (e.g. mute after a few seconds of silence with enough lead time, or lower amp gain). **(owner: keep for now)**
- Default volume should be low (TamaPoke found the speaker loud).
- Mute/volume is a compile-time default in v1, adjustable via the future web UI. **(builder's call)**

---

## 6. Persistence, time and catch-up

### 6.1 What gets saved

- The full tamalib state (registers, RAM, display RAM, I/O, timers, interrupt state, tick counter; TamaTool's `state.c` shows which fields matter).
- A wall-clock timestamp: Unix time from NTP if synced, otherwise "unknown".
- The ROM's SHA-1, a format version and a CRC32.
- The size is about 1 KB.

### 6.2 When it saves

- **After interaction:** a few seconds (≈3 s) after the last button release.
- **Periodically:** every few minutes (≈5 min). **(builder's call on exact values)**
- **On low battery**, before sleeping.
- **Before a reboot** for a ROM switch.
- There is no save-on-power-off, because the power button can't be detected.

### 6.3 Where it saves

- A dedicated raw flash partition (e.g. `saves`, 256 KB) used as a **ring of slots**. Each slot holds a sequence number, ROM SHA-1, timestamp, payload and CRC.
- On boot, load the newest slot with a valid CRC for the active ROM. A torn write simply falls back to the previous slot.
- Writes are spread across the partition, so there's no flash-wear concern for decades.
- Exact slot and sector layout is the builder's call.
- Saves are **per ROM** (keyed by SHA-1), so a P1 pet and a P2 pet each persist.
- Each save also carries a **pet ID**, always 0 in v1. That lets the next version hold several pets per model without changing the save format. **(builder's call)**
- **Saves never go to the SD card.**

### 6.4 Boot and catch-up sequence

1. Hold GPIO3 high, init the display, show a boot screen.
2. Mount the SD card if present. Determine the active ROM (§7). Load the ROM from the internal-flash ROM cache.
3. Load the newest valid save for that ROM, or start fresh (a new egg, as the ROM does on reset).
4. Connect Wi-Fi and get NTP time (wait up to ~10–15 s, builder's call).
5. **If both the save's timestamp and current NTP time are known**, compute the gap and run tamalib at speed 0 until the emulated tick counter has advanced by gap × 32768 ticks. Show the catch-up screen and ignore input. Save when done.
6. **If either time is unknown**, resume exactly where the save left off. The pet was frozen for the gap, and the user may need to fix the in-game clock.
7. Run in real time.

**What counts as "known time"** **(builder's call)**:
- NTP-synced time, **or** the ESP32's own system clock when it survived since the last NTP sync. That clock keeps running through software resets (e.g. the reboot for a ROM switch) and deep sleep (the low-battery path), but not through a power-button cut.
- Each save records whether its timestamp was trustworthy.
- After a true power cut, only NTP counts.

**Rules:**
- Catch-up must be faithful. The pet can get hungry, sick or die during the replay, exactly as if it had kept running. No artificial caps.
- **Catch-up progress is saved periodically** (e.g. every few minutes of wall time). Each such save is stamped with a *virtual* time: the original save time plus the emulated time replayed so far. A power cut mid-catch-up then loses at most a few minutes of replay. **(builder's call)**
- If NTP becomes available only after real-time play has resumed, **don't** retroactively catch up; just record real time in subsequent saves. **(builder's call)**
- During real-time play, the emulator's time base is `esp_timer` (crystal-driven). NTP only supplies the wall-clock stamps written into saves. Drift correction isn't needed in v1.

### 6.5 Wi-Fi

- Station mode, kept connected (the device is mostly on USB).
- Credentials live in NVS, set via the host tool over USB (§7.4). There's no setup hotspot or captive portal in v1. **(builder's call)**
- SNTP server: `pool.ntp.org` by default (configurable later).

---

## 7. ROMs

### 7.1 Supported in v1

Recognised by SHA-1 of the 12,288-byte ROM file. The format is the same as MAME's and TamaTool's: each 12-bit instruction stored as a 16-bit big-endian word.

| Model | SHA-1 | CRC32 | Icon set |
|---|---|---|---|
| P1 (Gen 1, world) | `4b4979cf92dc9d2fb6d7295a38f209f3da144f72` | `5c864cb1` | shared set, syringe |
| P2 (Gen 2, Japan) | `09e5101b37636a314fc599d5d69b4846721b3c88` | `9f97539e` | shared set, bottle and spoon |

Known but not enabled in v1:
- Angel: `f5899bb7717756ac581451cf16cf97d909961c5c`, 16,384 bytes
- Mothra: `74c1f6761724b7cbda8bca3113db78586b786d2d`, 16,384 bytes
- E0C6S46 test ROM: `6e13d015113e16198c0059b9d0c38d7027ae7324`. Always ignore it.

All of these are taken from MAME `src/mame/handheld/hh_e0c6200.cpp`. The owner's dumps in `roms/*.zip` (MAME-format zips) match these hashes.

**Unknown ROMs are rejected** with a message showing their SHA-1. **(builder's call)**

### 7.2 SD card layout

- ROMs live in `/tamaorb/roms/*.bin` on a FAT-formatted card.
- Other files on the card are ignored.

### 7.3 Active ROM selection

**The SD card is a library, not the source of truth** **(builder's call)**:
- **The remembered active ROM (NVS + internal-flash cache) always wins at boot**, whatever is on the card. Changing the card's contents never switches pets silently.
- Only when no active ROM is remembered yet (first boot) does the card decide:
  - one valid ROM: use it;
  - several: show the picker.
- Later the picker is reached by long-pressing BOOT, and lists what's on the card.
- The choice is remembered in NVS.
- `remove` of the active ROM deletes it from the card only. The pet keeps running from the cache.
- **SD card optional:** if no card is present, `push` writes the ROM straight into the internal cache and makes it active.
- Switching ROMs saves the current pet, then **reboots**.
- **Inactive pets keep aging.** **(decided)** A pet that isn't on screen isn't paused. When you switch back to it, the normal catch-up (§6.4) replays everything since its last save. Long absences mean long catch-ups, and the pet may not survive them. That's accepted (see §12).
- The active ROM is **copied to an internal-flash cache** (a small partition, or NVS blob, builder's call). The pet keeps running if the SD card is removed or fails.

### 7.4 Host tool: `tools/tamaorb-rom.py`

- Python 3, depends only on `pyserial`. It talks to the device over the USB-Serial/JTAG port it already uses for flashing and logs.
- **Commands:**
  - `push <file.bin|file.zip>…`: accepts raw `.bin` files or MAME zips. From zips it extracts only recognised game ROMs, skipping `test.bin` and `.svg`.
  - `list`
  - `remove <name>`
  - `wifi <ssid> [--password …]`
  - (optional) `status`: Wi-Fi, NTP, battery and active ROM.
- **The device verifies** the SHA-1 before writing to `/tamaorb/roms/`, and rejects unknown ROMs.
- **Protocol:** a framed protocol over the shared serial port that coexists with ESP-IDF's text log output, e.g. lines with a unique prefix carrying base64 chunks plus a checksum, with acknowledgements. Design details are the builder's call.
- Making the ball appear as a USB drive (TinyUSB MSC) was rejected: it would take over the USB-Serial/JTAG port used for flashing and logs.

---

## 8. Architecture

**ESP-IDF project layout (builder's call on exact names):**

```
tamaorb/
  main/                     app glue: boot sequence, task creation, wiring
  components/
    tamalib/                git submodule (unmodified) + our hal_types.h beside it
    tama_core/              portable: emulator runner (real-time + catch-up), frame/tone/button
                            state, save-state (de)serialisation, ROM identification table
    ui/                     portable: 240x240 renderer, screens, touch→button mapping, timeout logic
    persist/                save-slot ring on a flash partition, ROM cache
    net/                    Wi-Fi STA, SNTP (web/API/MCP later)
    hostlink/               USB serial protocol (ROM push, Wi-Fi setup, status)
    power/                  battery, USB sense, light sleep, low-battery handling
    board/                  board interface header + per-board implementations
      spotpear_ball_128/
  tools/tamaorb-rom.py
  test/host/                Linux build of tama_core + ui + persist logic with Unity tests
  docs/                     hardware notes, architecture decisions (ADRs), this spec
```

**Rules:**
- `tama_core` and `ui` **never include ESP-IDF or board headers**. They build and run on Linux.
- `ui` renders into a buffer and calls a board display-flush function.
- `persist` has a thin storage interface so its slot logic is host-testable.
- **Board interface** (one implementation per device), roughly: display init/flush (rectangles of RGB565), backlight level, touch read (up to 2 points), tone on/off and frequency, volume, battery mV, USB present, SD mount, BOOT button state. Choose the board at build time (Kconfig).
- **Tasks:**
  - Emulator task pinned to **core 1** at high priority, real-time loop.
  - UI, Wi-Fi, hostlink, power and persist work on **core 0**.
  - Shared frame/tone/button state crosses cores safely: a double-buffered frame plus an atomic or locked button bitmap.
  - Saving takes a consistent snapshot from the emulator task (e.g. the emulator task copies its state into a buffer on request).
- **Rendering:** only the LCD area and icons change, so redraw just the changed region. Full-frame RGB565 is 115 KB; use DMA-capable internal RAM bands or PSRAM as the builder sees fit. ~30 fps while the screen is on, nothing while it's off.
- **Future web UI/API/MCP:** they become another client of the same internal interface the touchscreen uses: press/release buttons, read frame and icons, read status, manage ROMs. Design that interface as a small C API in `tama_core`/`main`, not as touch-specific code.
- **Partition table** (16 MB): `nvs`, `phy_init`, app partition(s) (sized to allow two OTA slots later), `saves` (~256 KB), `romcache` (~64 KB), and the rest free. Exact sizes are the builder's call.

---

## 9. Power

- **On USB** (GPIO41 = 1): normal operation, Wi-Fi connected, screen timeout active.
- **On battery** (GPIO41 = 0): same behaviour. Use ESP-IDF power management (automatic light sleep, Wi-Fi modem sleep) where it doesn't disturb emulator timing. The emulator must stay real time; measure the effect.
- **Battery percentage:** the xiaozhi raw-ADC table (§3), smoothed over several samples. It needs a proper discharge-curve calibration later. A battery/charging indicator is optional in v1.
- **Critical battery** (≈ the 0–5% region; exact threshold is the builder's call):
  1. Save immediately.
  2. Show the low-battery screen briefly.
  3. Turn off the backlight, amp and Wi-Fi.
  4. Enter **deep sleep** with a periodic timer wake (e.g. every 60 s). GPIO41 isn't an RTC pin, so it can't wake the chip directly.
  5. On each wake, check GPIO41. If USB is back, reboot and resume (normal boot with catch-up). Otherwise sleep again.
- Firmware power-off is impossible (§3). Deep sleep is the only way to protect the battery.

---

## 10. Testing strategy

**Host tests (Linux, Unity, run in CI):**
- save-state serialise/deserialise round trip;
- slot-ring recovery from torn or corrupt writes;
- ROM SHA-1 identification;
- touch-zone mapping, including two points and zone changes;
- screen-timeout state machine (first touch wakes only);
- renderer golden images (compare the 240×240 buffer to reference PNGs);
- catch-up tick arithmetic.

**ROM-dependent host tests:**
- Run tamalib with a real ROM headless: boots, renders, responds to buttons, save→restore gives identical subsequent frames, catch-up of N seconds equals N seconds of real-time stepping.
- These tests **skip automatically when no ROM is present** (path from an env var or `roms/`), so CI never needs a ROM.

**CI:**
- Firmware build with the `espressif/idf:v6.1.1` Docker image, plus host tests.
- Nothing that requires ROMs.

**On-device manual checklist per milestone**, e.g. orientation, touch zones, two-finger A+C, sound, power-cut survival (press the power button at random moments), catch-up after a measured outage, SD removal, low battery.

---

## 11. Milestones (builder's proposal; ordering may change in the plan)

1. **Skeleton:** repo, GPL-3.0 license, README, `docs/hardware/spotpear-ball-128.md`, ADRs (framework, core, license, persistence), CI, host-test harness.
2. **Board bring-up:** display (orientation, colours), backlight, touch (2 points), BOOT button, test screen.
3. **Emulator on screen:** SD mount, ROM identify + cache, tamalib in real time, 6× renderer, touch → A/B/C. A playable P1.
4. **Sound:** ES8311 + amp, square-wave beeps.
5. **Persistence:** slot ring, save triggers, restore on boot. Survives power-button cuts.
6. **Time:** Wi-Fi + NTP, save timestamps, catch-up with progress screen.
7. **Host tool:** ROM push/list/remove, Wi-Fi setup.
8. **Power:** screen timeout and wake rules, battery reading, low-battery deep sleep, light sleep on battery.
9. **Polish:** P1/P2 icon sets and final LCD look (designed with the Artifact design tool, owner reviews), ROM picker, messages, docs.

---

## 12. Separate tracks (not v1)

**tamalib improvements** (to propose upstream):
- an opcode decode lookup table instead of the linear table scan;
- avoid `get_timestamp()` per instruction at speed 0;
- fast-forward through HALT to the next timer or interrupt event;
- fix the uninitialised `i` in `cpu_step()`;
- make `tamalib_init()` re-entrant.

Goal: catch-up well above the current ~29×.

**Known issue in v1** **(decided)**: long catch-ups block the device. A week off takes ≈6 h to replay, a month ≈25 h. There is no skip option, and v1 doesn't design around it. Faster tamalib is the planned fix.

**Later features:** multiple pets per model (several P1s, P2s, …; the save format's pet ID is ready for it), web UI, HTTP API, MCP server, OTA, Angel/Mothra and other models (icon sets, tap sensor on BOOT, 1 MHz OSC3 load), NTP-driven in-game clock set, battery discharge calibration, RGB LED use.

---

## 13. Left to the builder

- Exact colours, ghost-pixel shade and icon art (to be reviewed via the design tool), and the bitmap font.
- Save timing constants, slot layout, partition sizes, NTP wait time, low-battery threshold.
- Host-link framing protocol.
- Memory placement of render buffers, SPI clock (40 or 80 MHz; TamaPoke measured both working).
- Whether ES8311 setup uses `esp_codec_dev` or a minimal direct driver.
- Module and file naming within the structure in §8.
