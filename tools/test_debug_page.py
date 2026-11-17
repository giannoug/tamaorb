# SPDX-License-Identifier: GPL-3.0-or-later
"""The debugger page (components/web/page/debug.html): its disassembler matches tamalib, its labels match the doc."""
import json
import os
import pathlib
import re
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGE = (ROOT / "components" / "web" / "page" / "debug.html").read_text()
CPU = (ROOT / "components" / "tamalib" / "upstream" / "cpu.c").read_text()
DOC = (ROOT / "docs" / "pet-ram.md").read_text()


def block(name):
    m = re.search(r"/\*" + name + r"-BEGIN\*/(.*?)/\*" + name + r"-END\*/", PAGE, re.S)
    assert m, f"{name} block missing in debug.html"
    return m.group(1)


def tamalib_ops():
    masks = {k: int(v, 16) for k, v in re.findall(r"#define (MASK_\w+)\s+(0x[0-9A-F]+)", CPU)}
    table = CPU[CPU.index("static const op_t ops[] = {"):]
    table = table[:table.index("{NULL")]
    rows = re.findall(r'\{"[^"]*"\s*,\s*(0x[0-9A-F]+)\s*,\s*(\w+)\s*,\s*(\d+)\s*,\s*(0x[0-9A-F]+|0)\s*,', table)
    return [(int(c, 16), masks.get(m) or int(m, 16), int(s), int(a, 16) if a != "0" else 0) for c, m, s, a in rows]


def test_ops_table_matches_tamalib():
    ours = [tuple(e[:4]) for e in json.loads(block("OPS"))]
    theirs = tamalib_ops()
    assert len(theirs) == 108
    assert ours == theirs  # same entries in the same order: the first match wins, as in tamalib


@pytest.fixture(scope="module")
def disasm():
    node = shutil.which("node")
    if not node:
        if os.environ.get("CI"):
            pytest.fail("node is missing in CI: the disassembler tests must run there")
        pytest.skip("node not installed (CI's Linux runner has it)")
    helper = ROOT / "tools" / "debug_page_disasm.mjs"

    def run(*cases):
        out = subprocess.run([node, str(helper), str(ROOT / "components" / "web" / "page" / "debug.html")],
                             input=json.dumps(cases), capture_output=True, text=True, check=True).stdout
        return json.loads(out)
    return run


def test_disassembles_known_words(disasm):
    # (word, pc, word before it) -> text; the words are from tools/gen_testrom.py's program
    cases = [(0xE0F, 0x140, 0), (0xEC2, 0x143, 0), (0xC21, 0x14E, 0), (0x76C, 0x153, 0xDE6), (0xE6F, 0x118, 0),
             (0xE80, 0x116, 0), (0xF44, 0x14C, 0), (0xFF8, 0x11E, 0), (0xB10, 0x14D, 0), (0x66C, 0x149, 0),
             (0x512, 0x100, 0), (0x0A0, 0x205, 0xE43), (0xD2F, 0, 0), (0xF65, 0, 0), (0x512, 0x1100, 0)]
    want = ["LD A,0xF", "LD A,MX", "ADD MX,0x1", "JP NZ,0x16C", "LDPX MX,0xF", "LD XP,A", "SDF", "HALT", "LD X,0x10",
            "JP Z,0x16C", "CALZ 0x012", "JP 0x3A0", "NOT MX", "INC M(0x5)", "CALZ 0x1012"]  # CALZ: page 0 of the bank
    assert disasm(*cases) == want


def test_every_word_disassembles_to_something(disasm):
    texts = disasm(*[(w, 0x100, 0) for w in range(0x1000)])
    assert all(isinstance(t, str) and t for t in texts)


KNOWN = [("Clock", 0x10, 0x15), ("Call icon", 0x2D, 0x2D), ("Hunger", 0x40, 0x40), ("Happy", 0x41, 0x41),
         ("Care mistakes", 0x42, 0x42), ("Discipline", 0x43, 0x43), ("Weight", 0x46, 0x47), ("Sick", 0x48, 0x48),
         ("Light", 0x4B, 0x4B), ("Droppings", 0x4D, 0x4D), ("Age", 0x54, 0x55), ("Character", 0x5D, 0x5D),
         ("State", 0x5E, 0x5E)]


def test_known_addresses_match_doc():
    page = [tuple(e[:3]) for e in json.loads(block("KNOWN"))]
    assert page == KNOWN
    for name, first, last in KNOWN:
        for a in range(first, last + 1):
            assert f"`0x{a:02X}`" in DOC, f"{name} 0x{a:02X} not in docs/pet-ram.md"


def test_page_is_offline():
    urls = re.findall(r"https?://[^\s\"'<>)]+", PAGE)
    assert urls == [], urls
    assert not re.search(r"<link[^>]+rel=.?stylesheet", PAGE)
    assert not re.search(r"<script[^>]+src=", PAGE)
