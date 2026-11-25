"""Read Mothra's / Angel's meter pages out of a save_replay log.
usage: meter.py <log>
For every frame that is a meter page prints the time and what it shows: the hearts (filled count),
the bar (filled columns), or the age/weight page's two number lines as text (digits decoded where known)."""
import re
import sys

log = open(sys.argv[1]).read().splitlines()

# 5x7 digits as drawn on the meter's age/weight page (cols x..x+4, 7 rows); filled in as learned
DIGITS = {}


def frames():
    for i, line in enumerate(log):
        if line.startswith("--- t="):
            t = int(re.match(r"--- t=(\d+)", line).group(1))
            yield t, line, log[i + 1:i + 17]


def hearts(rows):
    # four hearts at x = 0-7, 8-15, 16-23, 24-31, rows 9-15; filled if the inside of row 11 is lit
    n = 0
    for k in range(4):
        inside = rows[11][k * 8 + 1:k * 8 + 6]
        if inside.count("#") >= 4:
            n += 1
    return n


def kind(rows):
    r0 = rows[0]
    if r0.startswith("...###..") and rows[3].startswith(".##...##"):
        return "agewt"
    if r0.startswith("...#..#...#"):
        return "hunger"
    if r0.startswith(".....#.#..#"):
        return "happy"
    if r0.startswith("....................#..#"):
        return "bar"
    return None


last = None
for t, head, rows in frames():
    k = kind(rows)
    if not k:
        continue
    if k in ("hunger", "happy"):
        what = f"{hearts(rows)} hearts"
    elif k == "bar":
        what = f"bar {sum(1 for c in rows[12][2:31] if c == '#')}/29 filled cols (row 12)"
    else:
        what = "\n  " + "\n  ".join(r[8:] for r in rows[0:7]) + "\n  --\n  " + "\n  ".join(r[8:] for r in rows[8:15])
    s = f"{k}: {what}"
    if s != last:
        print(f"t={t} {s}")
        last = s
