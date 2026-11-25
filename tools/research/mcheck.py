"""Count Mothra meter visits whose pages agree with the RAM dumped just before (mjoin.py output on stdin).
usage: mjoin.py <log> | mcheck.py
A page agrees if: hunger hearts = min(0x3E, 4) (4 if 0x3F != 0), happy = min(0x3D, 4), bar columns =
[0, 3, 7, 11, 14][min(0x3B, 4)], age = 10*0x5F + 0x5E, weight = 10*0x57 + 0x56. Prints totals and every mismatch."""
import re
import sys

lines = sys.stdin.read().splitlines()
ok = {}
bad = []
for head, vis in zip(lines[0::2], lines[1::2]):
    m = re.search(r"hunger (\d+)/(\d+) happy (\d+) bar (\d+) age (\d+) weight (\d+)", head)
    h3e, h3f, hap, bar, age, wt = (int(x) for x in m.groups())
    want = {"hunger": 4 if h3f else min(h3e, 4), "happy": min(hap, 4), "bar": [0, 3, 7, 11, 14][min(bar, 4)],
            "age": int(str(age)), "weight": int(str(wt))}
    got = dict(kv.split("=") for kv in vis.split())
    for k, v in got.items():
        if not v.strip() or "?" in v:
            continue
        if int(v) == want[k]:
            ok[k] = ok.get(k, 0) + 1
        else:
            bad.append(f"{head.strip()} | {k}: page {v}, RAM says {want[k]}")
print("agree:", ok, "mismatches:", len(bad))
for b in bad:
    print(b)
