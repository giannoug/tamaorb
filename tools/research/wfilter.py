"""Net RAM changes from save_replay 'watch' lines, ignoring writes undone within the same bucket.
usage: wfilter.py <log> [bucket_ms=100] [lo hi] [-x addr,addr,...]
Prints '<ms> 0x<addr>: old->new pcs=<writers>' once per bucket per address whose value changed over it."""
import re
import sys

args = [a for a in sys.argv[1:]]
excl = set()
if "-x" in args:
    i = args.index("-x")
    excl = {int(a, 16) for a in args[i + 1].split(",")}
    del args[i:i + 2]
log = args[0]
bucket = int(args[1]) if len(args) > 1 else 100
lo = int(args[2], 16) if len(args) > 2 else 0
hi = int(args[3], 16) if len(args) > 3 else 0x9F
cur = {}  # addr -> (bucket, old, new, pcs)
out = []


def flush(a):
    b, old, new, pcs = cur.pop(a)
    if old != new:
        out.append((b, a, old, new, pcs))


for line in open(log):
    m = re.match(r"watch (\d+) ms 0x([0-9A-F]{3}): ([0-9A-F])->([0-9A-F]) pc=([0-9A-F]{4})", line)
    if not m:
        continue
    t, a, old, new, pc = int(m.group(1)), int(m.group(2), 16), m.group(3), m.group(4), m.group(5)
    if not lo <= a <= hi or a in excl:
        continue
    b = t // bucket
    if a in cur and cur[a][0] != b:
        flush(a)
    if a not in cur:
        cur[a] = (b, old, new, [pc])
    else:
        bb, o, _, pcs = cur[a]
        if pc not in pcs:
            pcs.append(pc)
        cur[a] = (bb, o, new, pcs)
for a in list(cur):
    flush(a)
for b, a, old, new, pcs in sorted(out):
    print(f"{b * bucket} 0x{a:03X}: {old}->{new} pcs={','.join(pcs)}")
