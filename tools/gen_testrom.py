#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build testclock.bin: a tiny E0C6S46 program of our own, so CI can run the ROM tests without Bandai's ROMs.

Usage: tools/gen_testrom.py out.bin

The host tests (test/host/CMakeLists.txt) build it at configure time. It does what those tests need from the
P1 ROM, and nothing more:
- the pet's clock at RAM 0x10-0x15, the P1 layout: BCD seconds and minutes, hour = 16 * 0x15 + 0x14 (0-23);
- a 1 Hz clock-timer interrupt adds a second, carrying the way the P1 does (~25 instructions after 0x10 changes);
- while button A (K02) is held, the clock stands still: the stand-in for the P1's own clock menu;
- a row of LCD segments is lit, and it beeps for its first second (the buzzer path).
The opcodes are the E0C6S46's, as tamalib's ops[] table lists them (components/tamalib/upstream/cpu.c).
Output: 12288 bytes, 6144 big-endian 16-bit words, the layout tc_rom_decode reads.
"""
import sys

ROM_BYTES = 12288
A, B, MX, MY = 0, 1, 2, 3   # register operands r/q
SDF, RDF, EI, DI, RET, HALT = 0xF44, 0xF5B, 0xF48, 0xF57, 0xFDF, 0xFF8


class Asm:
    """Just the instructions the program uses. Code starts at the reset address, bank 0 page 1 step 0."""

    def __init__(self):
        self.prog = {}       # word address -> 12-bit opcode
        self.labels = {}
        self.fixups = []     # (address, opcode base, label)
        self.pc = 0x100

    def op(self, code):
        self.prog[self.pc] = code & 0xFFF
        self.pc += 1

    def at(self, addr):
        self.pc = addr

    def label(self, name):
        self.labels[name] = self.pc

    def jp(self, cond, name):
        """JP [C|NC|Z|NZ] to a label on the same page (the program keeps to page 1)."""
        base = {"": 0x000, "C": 0x200, "NC": 0x300, "Z": 0x600, "NZ": 0x700}[cond]
        self.fixups.append((self.pc, base, name))
        self.op(base)

    def link(self):
        for where, base, name in self.fixups:
            target = self.labels[name]
            if target >> 8 != where >> 8:
                raise ValueError(f"{name} is on another page")
            self.prog[where] = base | (target & 0xFF)
        self.fixups = []

    def ld_ri(self, r, i): self.op(0xE00 | r << 4 | i)      # LD r, #i
    def add_ri(self, r, i): self.op(0xC00 | r << 4 | i)     # ADD r, #i (decimal while D is set)
    def cp_ri(self, r, i): self.op(0xDC0 | r << 4 | i)      # CP r, #i
    def fan_ri(self, r, i): self.op(0xD80 | r << 4 | i)     # FAN r, #i: Z when r & i == 0
    def ld_rq(self, r, q): self.op(0xEC0 | r << 2 | q)      # LD r, q
    def ld_x(self, v): self.op(0xB00 | v)                   # LD X, #v (XH:XL)
    def ld_xp(self, r): self.op(0xE80 | r)                  # LD XP, r
    def ldpx_mx(self, i): self.op(0xE60 | i)                # LDPX MX, #i: M(X) = i, X++
    def ld_sph(self, r): self.op(0xFE0 | r)                 # LD SPH, r
    def ld_spl(self, r): self.op(0xFF0 | r)                 # LD SPL, r

    def xp(self, page):
        """XP = page (through A)."""
        self.ld_ri(A, page)
        self.ld_xp(A)


def program():
    a = Asm()
    # Vectors on page 1: reset at step 0, the clock timer's interrupt at step 2.
    a.at(0x100); a.jp("", "init")
    a.at(0x102); a.jp("", "clock_isr")

    a.at(0x110); a.label("init")
    a.op(DI)
    a.ld_ri(A, 0x8); a.ld_sph(A); a.ld_ri(A, 0x0); a.ld_spl(A)  # SP = 0x80, above the pet's RAM
    a.xp(0xE); a.ld_x(0x00)                                     # light a row of LCD segments
    for _ in range(8):
        a.ldpx_mx(0xF)
    a.xp(0xF); a.ld_x(0x10); a.ldpx_mx(0x8)                     # clock-timer interrupt mask: 1 Hz only (bit 3)
    a.ld_x(0x74); a.ldpx_mx(0x0)                                # buzzer frequency 0 (4096 Hz) ...
    a.ld_x(0x54); a.ld_ri(MX, 0x7)                              # ... and on (R43 low) until the first tick
    a.op(EI)
    a.label("idle"); a.op(HALT); a.jp("", "idle")

    a.at(0x140); a.label("clock_isr")
    a.xp(0xF); a.ld_x(0x00); a.ld_rq(A, MX)                     # read (and so clear) the clock-timer flags
    a.ld_x(0x54); a.ld_ri(MX, 0xF)                              # beep off
    a.ld_x(0x40); a.ld_rq(A, MX); a.fan_ri(A, 0x4)              # K02 (button A) low = held: the clock stands still
    a.jp("Z", "done")
    a.xp(0x0); a.op(SDF)                                        # decimal: 9 + 1 = 0 with carry
    a.ld_x(0x10); a.add_ri(MX, 1); a.jp("NC", "done")           # seconds, ones
    a.ld_x(0x11); a.add_ri(MX, 1); a.cp_ri(MX, 6); a.jp("NZ", "done"); a.ld_ri(MX, 0)   # seconds, tens
    a.ld_x(0x12); a.add_ri(MX, 1); a.jp("NC", "done")           # minutes, ones
    a.ld_x(0x13); a.add_ri(MX, 1); a.cp_ri(MX, 6); a.jp("NZ", "done"); a.ld_ri(MX, 0)   # minutes, tens
    a.op(RDF)                                                   # hours: binary, 0x14 low nibble, 0x15 high
    a.ld_x(0x14); a.add_ri(MX, 1); a.jp("NC", "h24")
    a.ld_x(0x15); a.add_ri(MX, 1)
    a.label("h24")
    a.ld_x(0x15); a.cp_ri(MX, 1); a.jp("NZ", "done")
    a.ld_x(0x14); a.cp_ri(MX, 8); a.jp("NZ", "done")            # 24:00 is 00:00
    a.ld_ri(MX, 0); a.ld_x(0x15); a.ld_ri(MX, 0)
    a.label("done"); a.op(RDF); a.op(EI); a.op(RET)
    a.link()
    return a


def build():
    out = bytearray(ROM_BYTES)
    for addr, code in program().prog.items():
        out[2 * addr], out[2 * addr + 1] = code >> 8, code & 0xFF
    return bytes(out)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    with open(argv[0], "wb") as f:
        f.write(build())
    return 0


if __name__ == "__main__":
    sys.exit(main())
