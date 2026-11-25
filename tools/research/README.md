# Research tools (Angel and Mothra RAM, docs/pet-ram.md)

Host-side helpers used to find the pets' stats in RAM, run from the repository root after `test/host/run.sh`
has built `build-host/save_replay` and `test/host/roms.sh` has put the dumps in `build-host/roms/`.
Their outputs (saves, logs, disassembly listings) go in `research/`, which git ignores: the listings and
sprites come from Bandai's ROMs, so they are not kept in the repository.

- `disasm.py` — E0C6S46/S48 disassembler (`python3 -I tools/research/disasm.py build-host/roms/tamamot.bin > research/tamamot.lst`)
- `poke.py`, `sweep.py` — change nibbles in a save and replay it (one value, or a sweep of values)
- `care.py`, `angscript.py`, `evo.py` — generate long care, neglect and evolution scripts for `save_replay`
- `meter.py`, `mjoin.py`, `mcheck.py`, `snap.py`, `view.py` — read meter pages and screens out of logs and check them against the RAM dumps
- `timeline.py`, `wfilter.py` — per-address value timelines from `ram` dumps; filter `watch` output
- `*.script` — the hand-written runs named in docs/pet-ram.md

Run Python with `-I`, and keep anything unpacked from `roms/*.zip` in its own folder under `research/`.
