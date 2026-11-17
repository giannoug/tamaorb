# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import gen_testrom as g  # noqa: E402

P1_SHA = "4b4979cf92dc9d2fb6d7295a38f209f3da144f72"
P2_SHA = "09e5101b37636a314fc599d5d69b4846721b3c88"


def words(rom):
    return [rom[i] << 8 | rom[i + 1] for i in range(0, len(rom), 2)]


def test_rom_is_tamaorb_sized_and_deterministic():
    a, b = g.build(), g.build()
    assert len(a) == 12288  # TC_ROM_BYTES: 6144 big-endian 16-bit words
    assert a == b
    assert all(w <= 0xFFF for w in words(a))


def test_encodings_match_tamalib_table():
    # Opcodes from tamalib's ops[] (components/tamalib/upstream/cpu.c); registers A=0 B=1 MX=2 MY=3.
    a = g.Asm()
    a.ld_ri(g.A, 5)
    a.add_ri(g.MX, 1)
    a.cp_ri(g.MX, 6)
    a.fan_ri(g.A, 4)
    a.ld_rq(g.A, g.MX)
    a.ld_x(0x10)
    a.ld_xp(g.A)
    a.ldpx_mx(0xF)
    a.op(g.SDF)
    a.op(g.EI)
    a.op(g.RET)
    a.op(g.HALT)
    assert [a.prog[0x100 + i] for i in range(12)] == [
        0xE05, 0xC21, 0xDE6, 0xD84, 0xEC2, 0xB10, 0xE80, 0xE6F, 0xF44, 0xF48, 0xFDF, 0xFF8]


def test_jumps_resolve_within_the_page():
    a = g.Asm()
    a.jp("NZ", "there")
    a.at(0x1A0)
    a.label("there")
    a.link()
    assert a.prog[0x100] == 0x7A0


def test_vectors_point_at_init_and_the_clock_interrupt():
    w = words(g.build())
    assert w[0x100] >> 8 == 0x0 and w[0x102] >> 8 == 0x0  # JP at reset (page 1, step 0) and the clock vector
    assert w[0x102] & 0xFF != w[0x100] & 0xFF


def test_not_a_bandai_rom():
    sha = hashlib.sha1(g.build()).hexdigest()
    assert sha not in (P1_SHA, P2_SHA)


def test_main_writes_the_file(tmp_path):
    out = tmp_path / "testclock.bin"
    assert g.main([str(out)]) == 0
    assert out.read_bytes() == g.build()
