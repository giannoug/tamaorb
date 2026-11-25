"""At every 'ram' dump of a save_replay log: chosen nibbles plus screen features of the last frame shown.
usage: snap.py <log> <addr,addr,...> [--frames] [--when expr]
Features: dark (>= 300 lit pixels: Mothra's light-off screen), skull (Mothra's sick icon top right),
z (a Z glyph anywhere: sleep), lit (pixel count). --frames prints the frame too; --when keeps only rows where the
Python expression is true (names: a<hex addr> e.g. a005, dark, skull, z, lit, h (hours))."""
import re
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
log = open(args[0]).read().splitlines()
addrs = [int(a, 16) for a in args[1].split(",")]
frames = "--frames" in sys.argv
when = None
if "--when" in sys.argv:
    when = sys.argv[sys.argv.index("--when") + 1]
    args = [a for a in args if a != when]

SKULL = [".#####.", "#.....#", "###.###", "#.....#", "##.#.##", ".#####.", ".#.#.#."]
SKULL2 = [".#####.", "#######", "#..#..#", "#######", "###.###", ".#####.", ".#.#.#."]  # its other frame
ZS = [["####", "...#", "..#.", ".#..", "#...", "####"], ["###", "..#", ".#.", "#..", "###"], ["####", "...#", "..#.", ".#..", "####"]]


def find(rows, pat):
    h, w = len(pat), len(pat[0])
    for y in range(17 - h):
        for x in range(33 - w):
            if all(rows[y + i][x:x + w] == pat[i] for i in range(h)):
                return True
    return False


ram, frame = {}, None
first = True
i = 0
print("h      " + " ".join(f"{a:03X}" for a in addrs) + "  dark skull z lit")
while i < len(log):
    line = log[i]
    if line.startswith("--- t="):
        frame = (line, log[i + 1:i + 17])
        i += 17
        continue
    m = re.match(r"=== ram at (\d+) ms", line)
    if m:
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
        if not frame:
            continue
        rows = frame[1]
        lit = sum(r.count("#") for r in rows)
        f = {"dark": lit >= 300, "skull": find(rows, SKULL) or find(rows, SKULL2), "z": any(find(rows, z) for z in ZS), "lit": lit, "h": t}
        env = dict(f)
        env.update({f"a{a:03X}".lower(): ram.get(a, -1) for a in addrs})
        env.update({f"a{a:03x}": ram.get(a, -1) for a in addrs})
        if when and not eval(when, {}, env):
            continue
        print(f"{t:6.2f} " + " ".join(f"{ram.get(a, -1):3X}" for a in addrs) +
              f"  {int(f['dark']):4d} {int(f['skull']):5d} {int(f['z']):1d} {lit:3d}")
        if frames:
            print(frame[0])
            print("\n".join(rows))
        continue
    i += 1
