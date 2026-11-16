# Notes for tamalib upstream (jcrona/tamalib @ ce304d5)

**Status (2026-11-17):** all of these are fixed in the fork [giannoug/tamalib `perf-and-fixes`](https://github.com/giannoug/tamalib/tree/perf-and-fixes) (pinned at `223ba64`), which TamaOrb now builds from, with the PR texts drafted there for jcrona/tamalib. With its opcode lookup table, catch-up on the ESP32-S3 went from 31.6x to 75.7x (300 emulated s of P1, three runs), and P1/P2 emulation stayed bit-identical to `ce304d5`. Saves now carry the timing fields (save format 2). The notes below are kept as filed.

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

## Speed-up ideas for catch-up (TamaOrb replays at ~31x on an ESP32-S3)

- **Opcode decode table:** `cpu_step()` scans the ~100-entry `ops[]` table linearly for every instruction. A 4096-entry lookup table (op index per 12-bit opcode) would remove that.
- **Fewer timestamp reads:** at speed 0, `wait_for_cycles()` still returns `g_hal->get_timestamp()` after every instruction, and the value is only used as the reference for the next real-time wait (which `tamalib_set_exec_mode()` re-syncs). TamaOrb now returns 0 from its HAL in fast mode: catch-up went from 24.0x to 30.5x on the ESP32-S3 (3,677x to 5,230x on a PC), with byte-identical state after an hour of P1 and P2. Upstream could skip the call when `speed_ratio == 0`.
- **Where the rest goes:** on a PC profile of catch-up after that change, `cpu_step()` itself is about 73% of the time and memory access (`get_memory`/`set_memory`) about 8%. TamaOrb's own wrapper costs under 1% (tested by caching its tick-counter pointer), so the next gains are inside `cpu_step()`: the opcode table and HALT ideas above.
- **HALT fast-forward:** while halted, jump straight to the next timer or interrupt deadline instead of stepping 5 cycles at a time.
