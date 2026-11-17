#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Configure, build and run the host tests. Uses ESP-IDF's bundled cmake if none is on PATH.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
if ! command -v cmake >/dev/null; then
    cm=$(ls -d "$HOME"/.espressif/tools/cmake/*/bin 2>/dev/null | head -1 || true)
    [ -n "$cm" ] && export PATH="$cm:$PATH"
fi
cmake -S "$here" -B "$root/build-host" -DCMAKE_BUILD_TYPE=Debug >/dev/null
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)  # nproc: Linux only
cmake --build "$root/build-host" -j"$jobs" >/dev/null
"$here/roms.sh" "$root/build-host/roms" >/dev/null 2>&1 || true
ctest --test-dir "$root/build-host" --output-on-failure "$@"
