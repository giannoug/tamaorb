<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ADR 0006: Build with PlatformIO

**Context.** TamaOrb should be easy to build for hobbyists who own a similar board and know VS Code or a terminal, but not ESP-IDF: installing an exact ESP-IDF release, sourcing `export.sh` and driving `idf.py` was the hard part. PlatformIO installs its own toolchain and builds and flashes with one command or one button on Windows, Linux and macOS. A spike built the unchanged project with PlatformIO's `espressif32` 8.0.0 (ESP-IDF 6.1.0) and ran it on the ball: same flash layout, same generated config apart from options new in 6.1.1, same memory use. PlatformIO's ESP-IDF support is maintained by PlatformIO, not Espressif, and trails ESP-IDF releases (6.0 support came about five months after 6.0).

**Decision.** ESP-IDF stays the framework (ADR 0001); PlatformIO is the only supported build tool.
- `platformio.ini` pins `platform = espressif32 @ 8.0.0` exactly. The firmware builds on the ESP-IDF that platform ships (6.1.0 today). Upgrades are a deliberate change, tested on the ball first.
- The ball is described by `boards/spotpear_ball_128.json`. Another device later is a board file, an `[env:…]` section and an entry in the existing `TAMAORB_BOARD` choice.
- Third-party code comes from Espressif's component registry through `idf_component.yml`, as before; the QR encoder (`espressif/qrcode`) moved there from a vendored copy.
- CI builds the firmware with PlatformIO.

**Consequences.**
- The CMake files stay: PlatformIO reads them. `idf.py` may still work, but it is not documented or tested.
- Embedded files are listed twice, in CMake and in `platformio.ini`.
- PlatformIO's builder misses two kinds of change: it re-runs CMake only for the root and `main/` CMake files, sdkconfig and the lock file, and CMake-time steps don't re-run for other inputs. `tools/pio_prebuild.py` forces a re-run when a component's build files change and re-gzips the web page before every build.
- New ESP-IDF releases reach TamaOrb only when PlatformIO ships them.
