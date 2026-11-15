# Hardware: SpotPear AI Voice Robot Ball (legs, touch, battery)

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

## CST816D register layout (measured)

| Reg | Meaning |
|---|---|
| `0x01` | gesture ID (0x04 swipe right, 0x05 tap, 0xAA two-finger gesture observed) |
| `0x02` | finger count, low nibble (0, 1 or 2) |
| `0x03` | point 1: event in bits 7..6 (0 down, 1 up, 2 contact), X[11:8] in bits 3..0 |
| `0x04` | point 1: X[7:0] |
| `0x05` | point 1: Y[11:8] in bits 3..0 |
| `0x06` | point 1: Y[7:0] |
| `0x09`–`0x0C` | point 2, same layout; touch ID in the upper nibble of `0x0B`; reads `0xFF` when unused |
| `0xA7` | chip ID `0xB6` |
| `0xA9` | firmware version `0x01` |
| `0xFE` | write `0x01` to disable auto sleep (CST816S convention) |

## Battery and USB notes

- Full battery on USB: ADC raw ≈ 2557, ≈ 2084 mV at the pin, ≈ 4.17 V at the cell (divider ×2).
- GPIO41 dropped to 0 on unplug and rose to 1 on replug while the battery was full.
- The power button cuts power even with USB connected; plugging in USB does not turn the board on.
