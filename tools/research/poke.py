"""Set RAM nibbles in a TamaOrb save record (as written by save_replay's dump), fixing the CRC.
usage: poke.py <in.sav> <out.sav> ADDR=VAL ...   (hex, e.g. 03D=4)
Layout: 42-byte header, payload, CRC32 of header+payload. The payload (tama_core walk, version 2) ends with
the memory buffer (550 bytes, RAM first, two nibbles per byte, even address in the low nibble) then 9 bytes."""
import struct
import sys
import zlib

HDR, MEMBUF, EXTRA = 42, (0x300 + 0x66 + 0x66 + 0x80) // 2, 9
rec = bytearray(open(sys.argv[1], "rb").read())
assert rec[:4] == b"TORB" and rec[4] == 1
plen = struct.unpack_from("<H", rec, 40)[0]
assert len(rec) == HDR + plen + 4
pay = HDR
assert rec[pay] == 2, "save layout version 2 expected"
mem = pay + plen - EXTRA - MEMBUF
for kv in sys.argv[3:]:
    a, v = (int(x, 16) for x in kv.split("="))
    assert 0 <= a < 0x300 and 0 <= v < 16
    i = mem + a // 2
    old = (rec[i] >> (4 * (a % 2))) & 0xF
    rec[i] = (rec[i] & ~(0xF << (4 * (a % 2)))) | (v << (4 * (a % 2)))
    print(f"0x{a:03X}: {old:X} -> {v:X}")
struct.pack_into("<I", rec, HDR + plen, zlib.crc32(bytes(rec[:HDR + plen])) & 0xFFFFFFFF)
open(sys.argv[2], "wb").write(rec)
