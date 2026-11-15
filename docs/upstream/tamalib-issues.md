# Notes for tamalib upstream (jcrona/tamalib @ ce304d5)

Drafts, ready to file. None of these are patched in TamaOrb; we work around them in `components/tama_core`.

## 1. `cpu_step()` reads an uninitialised local while the CPU is halted

In `cpu.c`, `cpu_step()` declares `u8_t i;` and only assigns it in the `!cpu_halted` branch. After the halted branch it evaluates `if (I && i != 0 && i != 58) process_interrupts();`, so whether interrupts are serviced while halted depends on stack contents. GCC reports `-Wmaybe-uninitialized` at line ~2002.

**Suggested fix:** initialise `i` to a value that isn't 0 (PSET) or 58 (EI) on the halted path, e.g. `u8_t i = 0xFF;` or a named constant. That services pending interrupts while halted, which matches the intent.

## 2. A second `tamalib_init()` leaves the CPU stuck halted

After a first `tamalib_init()`/run, calling `tamalib_release()` + `tamalib_init()` again (same process) leaves the emulated CPU permanently in HALT: every step only advances 5 cycles and the display never updates. File-level statics (e.g. `cpu_halted`, interrupt state, `previous_cycles` in `cpu_step()`) are not fully reset.

**Suggested fix:** reset all CPU statics in `cpu_init()`/`cpu_reset()`, or document that `tamalib_init()` is once per process.

## 3. Microsecond timestamps make real-time emulation run 0.35% fast

`wait_for_cycles()` computes `deadline = since + (cycles * ts_freq) / (cpu_frequency * speed_ratio)` with integer division per instruction. With `ts_freq = 1000000` (the README example) every 5-cycle step waits 152 µs instead of 152.59 µs, so a P1 runs ~5 minutes/day fast (measured on an ESP32-S3).

**Suggested fix:** accumulate the remainder, or recommend `ts_freq = 32768` (exact for OSC1). TamaOrb uses the latter.

## 4. `previous_cycles` is not part of `state_t`

Restoring a state replays the *current* `previous_cycles` before the first instruction, shifting timing by up to 12 cycles. That's harmless for play, but it makes save/restore not bit-exact. Exposing it in `state_t` (or resetting it on restore) would fix that.

## Speed-up ideas for catch-up (TamaOrb replays at ~25x on an ESP32-S3)

- **Opcode decode table:** `cpu_step()` scans the ~100-entry `ops[]` table linearly for every instruction. A 4096-entry lookup table (op index per 12-bit opcode) would remove that.
- **Fewer timestamp reads:** avoid `g_hal->get_timestamp()` per instruction at speed 0.
- **HALT fast-forward:** while halted, jump straight to the next timer or interrupt deadline instead of stepping 5 cycles at a time.
