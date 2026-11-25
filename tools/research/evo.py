"""Force a Mothra save's next evolution check (research only).
usage: evo.py <save> <name> [ADDR=VAL ...] [--hours N]
Reads the stage's target awake time (0x4C min ones, 0x4D min tens, 0x4E/0x4F hours low/high, as 0x059F compares
with the counters 0x48-0x4B), sets the counters 5 minutes short of it, sets the clock to 08:59:30 AM unless the
pokes set it, applies the pokes (care mistakes 0x37, bar 0x3B, stage 0x0B, weight 0x56/0x57...), and runs N hours
(default 3) with a ram dump every 10 min. Log: logs/evo_<name>.txt, end state: saves/evo_<name>_end.sav."""
import os
import subprocess
import sys

R = os.path.dirname(os.path.abspath(__file__))
args = [a for a in sys.argv[1:]]
hours = 3.0
if "--hours" in args:
    i = args.index("--hours")
    hours = float(args[i + 1])
    del args[i:i + 2]
save, name, pokes = args[0], args[1], args[2:]
data = open(save, "rb").read()
plen = int.from_bytes(data[40:42], "little")
mem = 42 + plen - 9 - 550
ram = [(data[mem + a // 2] >> (4 * (a % 2))) & 0xF for a in range(0x300)]
mins = ram[0x4D] * 10 + ram[0x4C] - 5
hrs = ram[0x4F] * 16 + ram[0x4E]
if mins < 0:
    mins += 60
    hrs -= 1
counters = [f"48={mins % 10:X}", f"49={mins // 10:X}", f"4A={hrs % 16:X}", f"4B={hrs // 16:X}"]
clock = [] if any(p.upper().startswith("1E=") for p in pokes) else ["1A=0", "1B=3", "1C=9", "1D=5", "1E=8", "1F=0"]
out = os.path.join(R, "saves", f"evo_{name}.sav")
subprocess.run([sys.executable, "-I", os.path.join(R, "poke.py"), save, out] + clock + counters + pokes, check=True,
               capture_output=True)
script = "ram " + " ".join(["ff600 ram"] * int(hours * 6)) + f" dump {os.path.join(R, 'saves', f'evo_{name}_end.sav')}"
with open(os.path.join(R, "logs", f"evo_{name}.txt"), "w") as f:
    subprocess.run(["build-host/save_replay", "build-host/roms/tamamot.bin", out, script], stdout=f, check=True)
print(f"target {ram[0x4D]}{ram[0x4C]} min, {hrs} h; counters {' '.join(counters)}")
tl = subprocess.run([sys.executable, "-I", os.path.join(R, "timeline.py"), os.path.join(R, "logs", f"evo_{name}.txt"),
                     "40", "0", "9F"], capture_output=True, text=True).stdout
for line in tl.splitlines():
    if line[:5] in ("0x00B", "0x060", "0x037", "0x03B", "0x005", "0x056", "0x057", "0x016"):
        print(line)
