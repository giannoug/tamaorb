"""Per-address value timelines from a save_replay log of 'ram' dumps.
usage: timeline.py <log> [max_changes] [lo hi]   (addresses in hex, default 0x000-0x0FF)
Prints, for each address with 1..max_changes changes, its values and the times (h) they changed."""
import re
import sys

log = open(sys.argv[1]).read().splitlines()
maxc = int(sys.argv[2]) if len(sys.argv) > 2 else 8
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0xFF
val, hist, t, first = {}, {}, 0.0, True
for line in log:
    m = re.match(r"=== ram at (\d+) ms", line)
    if m:
        if val:
            first = False
        t = int(m.group(1)) / 3.6e6
        continue
    m = re.match(r"0x([0-9A-F]{3}):((?: [0-9A-F]{4})+)$", line)  # the first, full dump
    if m and first:
        base = int(m.group(1), 16)
        digits = m.group(2).replace(" ", "")
        for i, d in enumerate(digits):
            val[base + i] = int(d, 16)
            hist[base + i] = [(t, int(d, 16))]
        continue
    m = re.match(r"0x([0-9A-F]{3}): ([0-9A-F])->([0-9A-F])$", line)
    if m:
        a, new = int(m.group(1), 16), int(m.group(3), 16)
        val[a] = new
        hist.setdefault(a, []).append((t, new))
for a in sorted(hist):
    if not lo <= a <= hi:
        continue
    changes = len(hist[a]) - 1
    if 1 <= changes <= maxc:
        print(f"0x{a:03X}: " + " ".join(f"{v:X}@{h:.2f}h" for h, v in hist[a]))
