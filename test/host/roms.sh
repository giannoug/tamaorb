#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Extracts the owner's local ROM dumps (never committed) for ROM-gated host tests.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
roms="$here/../../roms"
out="${1:-$here/../../build-host/roms}"
mkdir -p "$out"
[ -f "$roms/tama.zip" ]   && unzip -p "$roms/tama.zip" tama.bin     > "$out/tama.bin"   || true
[ -f "$roms/tamag2.zip" ] && unzip -p "$roms/tamag2.zip" tamag2.bin > "$out/tamag2.bin" || true
[ -f "$roms/tamaang.zip" ] && unzip -p "$roms/tamaang.zip" tamaang.bin > "$out/tamaang.bin" || true
[ -f "$roms/tamamot.zip" ] && unzip -p "$roms/tamamot.zip" tamamot.bin > "$out/tamamot.bin" || true
ls -l "$out"
