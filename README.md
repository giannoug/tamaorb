# TamaOrb

Authentic first-generation Tamagotchi (P1, P2) emulation for round ESP32-S3 devices, starting with the SpotPear 1.28" AI Voice Robot Ball. TamaOrb runs the original ROMs on [tamalib](https://github.com/jcrona/tamalib).

**No ROMs are included.** You need your own dumps; see `docs/superpowers/specs/2026-11-15-tamaorb-design.md` §7 for the supported SHA-1s.

Status: under development. Design: `docs/superpowers/specs/`, hardware notes: `docs/hardware/`.

## Build

```bash
git submodule update --init
. ~/esp/esp-idf-v6.1.1/export.sh   # ESP-IDF v6.1.1
idf.py set-target esp32s3 build
```

## License

GPL-3.0-or-later (see `LICENSE`). tamalib is GPL-2.0-or-later.
