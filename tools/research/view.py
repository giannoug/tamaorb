"""Condense a save_replay log: the screen just before each press / ram / clock / ff / end, and ram diffs
limited to an address range.
usage: view.py <log> [lo hi] [--all-frames]   (addresses in hex, default 0x000-0x09F)"""
import re
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
allf = "--all-frames" in sys.argv
log = open(args[0]).read().splitlines()
lo = int(args[1], 16) if len(args) > 1 else 0
hi = int(args[2], 16) if len(args) > 2 else 0x9F
frame, shown = None, None
i = 0


def flush(tag):
    global shown
    if frame and frame is not shown:
        print(frame[0])
        print("\n".join(frame[1:]))
        shown = frame


while i < len(log):
    line = log[i]
    if line.startswith("--- t="):
        frame = [line] + log[i + 1:i + 17]
        i += 17
        if allf:
            flush("")
        continue
    if line.startswith("=== press") or line.startswith("=== fast") or line.startswith("=== ram") \
            or line.startswith("=== clock") or line.startswith("=== watch") or line.startswith("=== dump") \
            or line.startswith("=== set"):
        if not line.startswith("=== clock") and not line.startswith("=== dump"):
            flush(line)
        print(line)
        i += 1
        continue
    m = re.match(r"0x([0-9A-F]{3}): ([0-9A-F])->([0-9A-F])$", line)
    if m:
        if lo <= int(m.group(1), 16) <= hi:
            print(line)
        i += 1
        continue
    m = re.match(r"0x([0-9A-F]{3}):((?: [0-9A-F]{4})+)$", line)
    if m:
        if int(m.group(1), 16) <= hi and int(m.group(1), 16) + 31 >= lo:
            print(line)
        i += 1
        continue
    m = re.match(r"watch (\d+) ms 0x([0-9A-F]{3}):", line)
    if m:
        if lo <= int(m.group(2), 16) <= hi:
            print(line)
        i += 1
        continue
    print(line)
    i += 1
flush("end")
