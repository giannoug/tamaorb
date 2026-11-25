"""Poke one RAM nibble at a time into a save, open the meter and report which pages changed.
usage: sweep.py <rom> <save> <lo> <hi> <value|+N> <script> [out_prefix]
For each address in lo..hi (hex): set it to value (or old+N mod 16), run the script, and compare every
meter page (meter.py's output) with the unpoked run. Prints the addresses whose pages differ, with the pages."""
import os
import subprocess
import sys

R = os.path.dirname(os.path.abspath(__file__))
rom, save, lo, hi, val, script = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16), sys.argv[5], sys.argv[6]
tmp = os.path.join(R, "saves", "sweep_tmp.sav")
log = os.path.join(R, "logs", "sweep_tmp.txt")


def ram_of(path):
    data = open(path, "rb").read()
    plen = int.from_bytes(data[40:42], "little")
    mem = 42 + plen - 9 - 550
    return [(data[mem + a // 2] >> (4 * (a % 2))) & 0xF for a in range(0x300)]


def pages(path):
    with open(log, "w") as f:
        r = subprocess.run(["build-host/save_replay", rom, path, script, "200", "600"], stdout=f, timeout=120)
    out = subprocess.run([sys.executable, "-I", os.path.join(R, "meter.py"), log], capture_output=True, text=True).stdout
    # drop timestamps: keep the page contents in order
    lines = []
    for line in out.splitlines():
        lines.append(line.split(" ", 1)[1] if line.startswith("t=") else line)
    return "\n".join(lines), r.returncode


base, _ = pages(save)
print("baseline:\n" + base)
ram = ram_of(save)
for a in range(lo, hi + 1):
    v = (ram[a] + int(val[1:])) % 16 if val.startswith("+") else int(val, 16)
    if v == ram[a]:
        continue
    subprocess.run([sys.executable, "-I", os.path.join(R, "poke.py"), save, tmp, f"{a:X}={v:X}"], capture_output=True)
    try:
        p, rc = pages(tmp)
    except subprocess.TimeoutExpired:
        print(f"0x{a:03X}={v:X}: timeout")
        continue
    if p != base:
        print(f"=== 0x{a:03X}: {ram[a]:X}->{v:X} rc={rc}")
        print(p)
    sys.stdout.flush()
