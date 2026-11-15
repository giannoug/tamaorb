# ADR 0002: tamalib as an unmodified submodule

**Context.** tamalib is the only maintained first-gen Tamagotchi core. The ESP32 ports in the wild use a heavily modified Arduino-Uno fork.

**Decision.** Pin upstream tamalib (`ce304d5`) as a git submodule and never edit it. All adaptation lives in `components/tama_core`.

**Consequences.**
- Two upstream bugs are worked around without patching (see `docs/upstream/tamalib-issues.md`):
  - an uninitialised local in `cpu_step()` (compiled with `-Wno-error=maybe-uninitialized`);
  - `tamalib_init()` can't run twice (ROM switches reboot; tests reset through a boot snapshot or forked processes).
- tamalib timestamps are in 1/32768 s. In microseconds, tamalib's integer division truncated every wait and ran the emulator 0.35% fast, measured on the device.
- A restored state can differ by up to 12 CPU cycles (~0.4 ms) of timing, because tamalib keeps the previous instruction's cycle count outside its state. TamaTool's saves have the same property.
