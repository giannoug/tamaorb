"""Mothra meter pages joined with the RAM of the last 'ram' dump before them.
usage: mjoin.py <log> [max]
For each meter visit prints the RAM values (hunger 3E/3F, happy 3D, bar 3B, age 5F:5E, weight 57:56) and what the
pages show: hearts, bar quarters, and the age and weight numbers read with a digit table learned from pokes."""
import re
import sys

log = open(sys.argv[1]).read().splitlines()
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10 ** 9

# 4-pixel-wide digits, 7 rows, as the meter draws them (learned from poked saves, see the doc)
DIG = {
    ".##.|#..#|#..#|#..#|#..#|#..#|.##.": 0,
    "..#.|.##.|..#.|..#.|..#.|..#.|..#.": 1,
    ".##.|#..#|...#|..#.|.#..|#...|####": 2,
    ".##.|#..#|...#|.##.|...#|#..#|.##.": 3,
    "..#.|.##.|#.#.|#.#.|#.#.|####|..#.": 4,
    "####|#...|#...|###.|...#|...#|###.": 5,
    ".##.|#..#|#...|###.|#..#|#..#|.##.": 6,
    "####|#..#|...#|..#.|..#.|..#.|..#.": 7,
    ".##.|#..#|#..#|.##.|#..#|#..#|.##.": 8,
    ".##.|#..#|#..#|.###|...#|#..#|.##.": 9,
}


def digit(rows, x):
    key = "|".join(r[x:x + 4] for r in rows)
    return DIG.get(key, "?" if "#" in key else " ")


def hearts(rows):
    return sum(1 for k in range(4) if rows[11][k * 8 + 1:k * 8 + 6].count("#") >= 4)


def kind(rows):
    if rows[0].startswith("...###..") and rows[3].startswith(".##...##"):
        return "agewt"
    if rows[0].startswith("...#..#...#"):
        return "hunger"
    if rows[0].startswith(".....#.#..#"):
        return "happy"
    if rows[0].startswith("....................#..#"):
        return "bar"
    return None


ram = {}
visit = {}
shown = 0
i = 0
while i < len(log):
    line = log[i]
    m = re.match(r"=== ram at (\d+) ms", line)
    if m:
        if visit:
            print(visit_line)
            print("   " + " ".join(f"{k}={v}" for k, v in visit.items()))
            shown += 1
            if shown >= limit:
                break
            visit = {}
        t = int(m.group(1)) / 3.6e6
        i += 1
        while i < len(log) and re.match(r"0x[0-9A-F]{3}:", log[i]):
            mm = re.match(r"0x([0-9A-F]{3}):((?: [0-9A-F]{4})+)$", log[i])
            if mm:
                base = int(mm.group(1), 16)
                for k, d in enumerate(mm.group(2).replace(" ", "")):
                    ram[base + k] = int(d, 16)
            else:
                mm = re.match(r"0x([0-9A-F]{3}): ([0-9A-F])->([0-9A-F])$", log[i])
                ram[int(mm.group(1), 16)] = int(mm.group(3), 16)
            i += 1
        visit_line = (f"{t:7.2f}h RAM hunger {ram.get(0x3E)}/{ram.get(0x3F)} happy {ram.get(0x3D)} bar {ram.get(0x3B)} "
                      f"age {ram.get(0x5F)}{ram.get(0x5E)} weight {ram.get(0x57)}{ram.get(0x56)} stage {ram.get(0xB):X}")
        continue
    if line.startswith("--- t="):
        rows = log[i + 1:i + 17]
        k = kind(rows)
        if k == "agewt":
            age_rows, wt_rows = rows[0:7], rows[8:15]
            visit["age"] = f"{digit(age_rows, 13)}{digit(age_rows, 19)}".strip()
            visit["weight"] = f"{digit(wt_rows, 13)}{digit(wt_rows, 19)}".strip()
        elif k in ("hunger", "happy"):
            visit[k] = hearts(rows)
        elif k == "bar":
            visit["bar"] = sum(1 for c in rows[12][2:31] if c == "#")
        i += 17
        continue
    i += 1
