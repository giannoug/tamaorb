"""Writes a Mothra care-loop script for save_replay.
usage: care.py <hours> <period_min> <start_hhmm> [options]  > script
  options: snack=1 (a snack per block), meal=1, scold=1, clean=1, fairy=3 (doses per block), game=0,
  sleep=20-8 (night window, 24 h clock), meter=N (meter every N blocks), nolight=1 (never switch the light off),
  food_cursor=top|bottom, light_cursor=top|bottom
Never presses C on the main screen: in this emulation C there toggles the ROM's fast mode (0x008 bit 0, K13
reads low), which runs the pet's clock and timers ~20 min per second. Menus are left by their own 10 s
timeout instead (IDLE). The food menu remembers its cursor (0x023), so the script tracks it (A toggles); the
light menu's cursor starts on the current light state.
  snack=N: a snack every N awake blocks (0 never; snacks raise the sickness odds via 0x062)
  game=1: a game per block (about half of them add a heart), dump_h=N dump_prefix=P: a save every N hours"""
import sys

hours, period, start = float(sys.argv[1]), float(sys.argv[2]), sys.argv[3]
opt = dict(a.split("=") for a in sys.argv[4:])
meal = opt.get("meal", "1") == "1"
snack_every = int(opt.get("snack", "1"))  # a snack every N awake blocks (0: never)
scold = opt.get("scold", "1") == "1"
clean = opt.get("clean", "1") == "1"
game = int(opt.get("game", "0"))  # games per awake block
dump_every = float(opt.get("dump_h", "0"))  # dump the state every N hours as <dump_prefix>_<h>h.sav
dump_prefix = opt.get("dump_prefix", "care")
fairy = int(opt.get("fairy", "3"))
night = [int(x) for x in opt.get("sleep", "20-8").split("-")]
meter_every = int(opt.get("meter", "0"))
nolight = opt.get("nolight", "0") == "1"
food_cur = {"top": "meal", "bottom": "snack"}[opt.get("food_cursor", "top")]
light_cur = opt.get("light_cursor", "top")
start_min = int(start[:2]) * 60 + int(start[2:])
IDLE = 12000  # ms: the ROM leaves a menu after 10 s without a press

out = []
t = 0.0  # emulated seconds since the script start (press = 200 ms hold + 600 ms gap)


def emit(s, secs):
    global t
    out.append(s)
    t += secs


def icon(n):
    emit(" ".join(["A w900"] * n), n * 1.7)


def idle():
    emit(f"w{IDLE}", IDLE / 1000)


def food(item):
    global food_cur
    icon(2)
    emit("B w1500", 2.3)
    if food_cur != item:
        emit("A w1200", 2.0)
        food_cur = item
    emit("B w14000", 14.8)
    idle()


def asleep(clock_min):
    h = clock_min // 60 % 24
    s, w = night
    return h >= s or h < w


blocks = int(hours * 60 / period)
lit = True
n = 0
for b in range(blocks):
    target = (b + 1) * period * 60
    wait = max(1, int(target - t))
    emit(f"ff{wait} w100", wait + 0.1)
    clock = start_min + int(t // 60)
    if dump_every and b and (b * period) % int(dump_every * 60) == 0:
        emit(f"dump research/saves/{dump_prefix}_{int(b * period / 60)}h.sav", 0)
    if asleep(clock):
        if lit and not nolight:
            # the light menu's cursor starts on the current state (light on: top), so A always moves to "off"
            icon(7)
            emit("B w1500 A w1200 B w3000", 8.1)
            idle()
            lit = False
        emit("ram", 0)
        continue
    lit = True  # the ROM turns the light on when the pet wakes
    if meal:
        food("meal")
    if snack_every and n % snack_every == 0:
        food("snack")
    for _ in range(game):
        icon(3)
        emit("B w3000 " + "B w4000 " * 5 + "w8000", 3.8 + 5 * 4.8 + 8)
        idle()
    if clean:
        icon(4)
        emit("B w8000", 8.8)
        idle()
    if scold:
        icon(5)
        emit("B w6000", 6.8)
        idle()
    for _ in range(fairy):
        icon(6)
        emit("B w9000", 9.8)
        idle()
    emit("ram", 0)
    n += 1
    if meter_every and n % meter_every == 0:
        icon(1)
        emit("B w1500 A w2300 A w2300 A w2300", 9.7)
        idle()
print(" ".join(out))
print(f"# blocks={blocks} t={t:.0f}s food_cursor={food_cur} light_cursor={light_cur}", file=sys.stderr)
