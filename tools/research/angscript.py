"""Angel run scripts (print a save_replay script).
usage: angscript.py feed <hours> <period_min> [snack=1] [clean=0] [step=60]
  Every period: a meal (A A B B) and a snack (A A B A B), C C back, then 'ff<step> ram' until the next period.
Angel icons (A from the main screen): bit 0 meter, 1 food, 2 game, 3 bathroom, 4 discipline, 5 medicine, 6 light."""
import sys

mode = sys.argv[1]
opt = dict(a.split("=") for a in sys.argv[4:])
if mode == "feed":
    hours, period = float(sys.argv[2]), float(sys.argv[3])
    step = int(opt.get("step", "60"))
    s = ["w1000 ram"]
    for blk in range(int(hours * 60 / period)):
        s.append("A w1000 A w1000 B w1500 B w10000 C w1000 C w1000")
        if opt.get("snack", "1") == "1":
            s.append("A w1000 A w1000 B w1500 A w1000 B w10000 C w1000 C w1000")
        if opt.get("clean", "0") == "1":
            s.append("A w1000 A w1000 A w1000 A w1000 B w8000 C w1000 C w1000")
        s.append("ram")
        for _ in range(int(period * 60 / step) - 1):
            s.append(f"ff{step} ram")
    print(" ".join(s))
elif mode == "games":
    # angscript.py games <hours> <period_min> [games=3] [clean=0]: a meal and N games per period, ram after
    hours, period = float(sys.argv[2]), float(sys.argv[3])
    game = "A w1000 A w1000 A w1000 B w3000 " + "B w3000 " * 7 + "w5000 C w1000 C w1000"
    s = ["w1000 ram"]
    blk_s = 18 + int(opt.get("games", "3")) * 37 + (13 if opt.get("clean", "0") == "1" else 0)
    for blk in range(int(hours * 60 / period)):
        s.append("A w1000 A w1000 B w1500 B w10000 C w1000 C w1000")
        s += [game] * int(opt.get("games", "3"))
        if opt.get("clean", "0") == "1":
            s.append("A w1000 A w1000 A w1000 A w1000 B w8000 C w1000 C w1000")
        s.append(f"ram ff{int(period * 60 - blk_s)} ram")
    print(" ".join(s))
