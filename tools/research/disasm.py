"""E0C6S4x disassembler for the TamaOrb ROM dumps (big-endian 16-bit words, low 12 bits used).
usage: disasm.py <rom.bin> [from to]   (hex word addresses, 13-bit: bank<<12 | page<<8 | step)
The opcode table follows tamalib's cpu.c (first match wins). JP/CALL targets use the PSET just before them.
R(0..3) = A, B, MX, MY."""
import sys

R = ["A", "B", "MX", "MY"]
# (name, code, mask, kind)   kind tells how to print the argument(s)
OPS = [
    ("PSET", 0xE40, 0xFE0, "i5"), ("JP", 0x000, 0xF00, "jp"), ("JP C", 0x200, 0xF00, "jp"),
    ("JP NC", 0x300, 0xF00, "jp"), ("JP Z", 0x600, 0xF00, "jp"), ("JP NZ", 0x700, 0xF00, "jp"),
    ("JPBA", 0xFE8, 0xFFF, ""), ("CALL", 0x400, 0xF00, "call"), ("CALZ", 0x500, 0xF00, "calz"),
    ("RET", 0xFDF, 0xFFF, ""), ("RETS", 0xFDE, 0xFFF, ""), ("RETD", 0x100, 0xF00, "i8"),
    ("NOP5", 0xFFB, 0xFFF, ""), ("NOP7", 0xFFF, 0xFFF, ""), ("HALT", 0xFF8, 0xFFF, ""),
    ("INC X", 0xEE0, 0xFFF, ""), ("INC Y", 0xEF0, 0xFFF, ""),
    ("LD X", 0xB00, 0xF00, "i8"), ("LD Y", 0x800, 0xF00, "i8"),
    ("LD XP,", 0xE80, 0xFFC, "r"), ("LD XH,", 0xE84, 0xFFC, "r"), ("LD XL,", 0xE88, 0xFFC, "r"),
    ("LD YP,", 0xE90, 0xFFC, "r"), ("LD YH,", 0xE94, 0xFFC, "r"), ("LD YL,", 0xE98, 0xFFC, "r"),
    ("LD XP->", 0xEA0, 0xFFC, "r"), ("LD XH->", 0xEA4, 0xFFC, "r"), ("LD XL->", 0xEA8, 0xFFC, "r"),
    ("LD YP->", 0xEB0, 0xFFC, "r"), ("LD YH->", 0xEB4, 0xFFC, "r"), ("LD YL->", 0xEB8, 0xFFC, "r"),
    ("ADC XH", 0xA00, 0xFF0, "i4"), ("ADC XL", 0xA10, 0xFF0, "i4"), ("ADC YH", 0xA20, 0xFF0, "i4"),
    ("ADC YL", 0xA30, 0xFF0, "i4"), ("CP XH", 0xA40, 0xFF0, "i4"), ("CP XL", 0xA50, 0xFF0, "i4"),
    ("CP YH", 0xA60, 0xFF0, "i4"), ("CP YL", 0xA70, 0xFF0, "i4"),
    ("LD", 0xE00, 0xFC0, "ri"), ("LD", 0xEC0, 0xFF0, "rq"),
    ("LD A,M", 0xFA0, 0xFF0, "m"), ("LD B,M", 0xFB0, 0xFF0, "m"), ("LD M->A", 0xF80, 0xFF0, "m"),
    ("LD M->B", 0xF90, 0xFF0, "m"),
    ("LDPX MX", 0xE60, 0xFF0, "i4"), ("LDPX", 0xEE0, 0xFF0, "rq"), ("LDPY MY", 0xE70, 0xFF0, "i4"),
    ("LDPY", 0xEF0, 0xFF0, "rq"), ("LBPX", 0x900, 0xF00, "i8"),
    ("SET F", 0xF40, 0xFF0, "i4"), ("RST F", 0xF50, 0xFF0, "i4"),
    ("SCF", 0xF41, 0xFFF, ""), ("RCF", 0xF5E, 0xFFF, ""), ("SZF", 0xF42, 0xFFF, ""), ("RZF", 0xF5D, 0xFFF, ""),
    ("SDF", 0xF44, 0xFFF, ""), ("RDF", 0xF5B, 0xFFF, ""), ("EI", 0xF48, 0xFFF, ""), ("DI", 0xF57, 0xFFF, ""),
    ("INC SP", 0xFDB, 0xFFF, ""), ("DEC SP", 0xFCB, 0xFFF, ""),
    ("PUSH", 0xFC0, 0xFFC, "r"), ("PUSH XP", 0xFC4, 0xFFF, ""), ("PUSH XH", 0xFC5, 0xFFF, ""),
    ("PUSH XL", 0xFC6, 0xFFF, ""), ("PUSH YP", 0xFC7, 0xFFF, ""), ("PUSH YH", 0xFC8, 0xFFF, ""),
    ("PUSH YL", 0xFC9, 0xFFF, ""), ("PUSH F", 0xFCA, 0xFFF, ""),
    ("POP", 0xFD0, 0xFFC, "r"), ("POP XP", 0xFD4, 0xFFF, ""), ("POP XH", 0xFD5, 0xFFF, ""),
    ("POP XL", 0xFD6, 0xFFF, ""), ("POP YP", 0xFD7, 0xFFF, ""), ("POP YH", 0xFD8, 0xFFF, ""),
    ("POP YL", 0xFD9, 0xFFF, ""), ("POP F", 0xFDA, 0xFFF, ""),
    ("LD SPH,", 0xFE0, 0xFFC, "r"), ("LD SPL,", 0xFF0, 0xFFC, "r"), ("LD SPH->", 0xFE4, 0xFFC, "r"),
    ("LD SPL->", 0xFF4, 0xFFC, "r"),
    ("ADD", 0xC00, 0xFC0, "ri"), ("ADD", 0xA80, 0xFF0, "rq"), ("ADC", 0xC40, 0xFC0, "ri"), ("ADC", 0xA90, 0xFF0, "rq"),
    ("SUB", 0xAA0, 0xFF0, "rq"), ("SBC", 0xD40, 0xFC0, "ri"), ("SBC", 0xAB0, 0xFF0, "rq"),
    ("AND", 0xC80, 0xFC0, "ri"), ("AND", 0xAC0, 0xFF0, "rq"), ("OR", 0xCC0, 0xFC0, "ri"), ("OR", 0xAD0, 0xFF0, "rq"),
    ("XOR", 0xD00, 0xFC0, "ri"), ("XOR", 0xAE0, 0xFF0, "rq"), ("CP", 0xDC0, 0xFC0, "ri"), ("CP", 0xF00, 0xFF0, "rq"),
    ("FAN", 0xD80, 0xFC0, "ri"), ("FAN", 0xF10, 0xFF0, "rq"), ("RLC", 0xAF0, 0xFF0, "r2"), ("RRC", 0xE8C, 0xFFC, "r"),
    ("INC M", 0xF60, 0xFF0, "m"), ("DEC M", 0xF70, 0xFF0, "m"),
    ("ACPX", 0xF28, 0xFFC, "r"), ("ACPY", 0xF2C, 0xFFC, "r"), ("SCPX", 0xF38, 0xFFC, "r"), ("SCPY", 0xF3C, 0xFFC, "r"),
    ("NOT", 0xD0F, 0xFCF, "rn"),
]


def load(path):
    d = open(path, "rb").read()
    return [((d[2 * i] << 8) | d[2 * i + 1]) & 0xFFF for i in range(len(d) // 2)]


def dis(words, pc, np):
    """Returns (text, new_np or None, target or None)."""
    op = words[pc]
    for name, code, mask, kind in OPS:
        if op & mask == code:
            break
    else:
        return f"??? {op:03X}", None, None
    a = op & ~mask & 0xFFF
    tgt = None
    if kind == "i5":
        return f"PSET {a:02X}", a, None
    if kind == "jp":
        page = np if np is not None else (pc >> 8)
        tgt = (page << 8) | a
        t = f"{name} {tgt:04X}"
    elif kind == "call":
        page = np if np is not None else (pc >> 8)
        tgt = ((pc & 0x1000) | ((page & 0xF) << 8)) | a
        t = f"CALL {tgt:04X}"
    elif kind == "calz":
        tgt = (pc & 0x1000) | a
        t = f"CALZ {tgt:04X}"
    elif kind == "i8":
        t = f"{name} {a:02X}"
    elif kind == "i4":
        t = f"{name} {a:X}"
    elif kind == "r":
        t = f"{name} {R[a & 3]}"
    elif kind == "r2":
        t = f"{name} {R[a & 3]}"
    elif kind == "rn":
        t = f"NOT {R[(op >> 4) & 3]}"
    elif kind == "ri":
        t = f"{name} {R[(op >> 4) & 3]},{op & 0xF:X}"
    elif kind == "rq":
        t = f"{name} {R[(op >> 2) & 3]},{R[op & 3]}"
    elif kind == "m":
        t = f"{name}({a:X})"
    else:
        t = name
    return t, None, tgt


def listing(words, lo=0, hi=None):
    hi = len(words) - 1 if hi is None else hi
    np = None
    out = []
    for pc in range(lo, hi + 1):
        t, newnp, tgt = dis(words, pc, np)
        out.append((pc, words[pc], t, tgt))
        np = newnp if newnp is not None else None
        if newnp is not None:
            np = newnp
    return out


if __name__ == "__main__":
    w = load(sys.argv[1])
    lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else len(w) - 1
    for pc, op, t, tgt in listing(w, lo, hi):
        print(f"{pc:04X}  {op:03X}  {t}")
