# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tamaorb-rom.py. Uses synthetic data only: no real ROMs."""
import base64
import hashlib
import importlib.util
import io
import pathlib
import zipfile

import pytest

_spec = importlib.util.spec_from_file_location("tamaorb_rom", pathlib.Path(__file__).with_name("tamaorb-rom.py"))
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)

FAKE = bytes((i * 7) & 0xFF for i in range(12288))
FAKE_SHA = hashlib.sha1(FAKE).hexdigest()


class FakePort:
    """Replies to each written request from a script; interleaves log noise."""

    def __init__(self, replies):
        self.replies = list(replies)
        self.written = []
        self.pending = []

    def write(self, data):
        self.written.append(data.decode().rstrip("\n"))
        self.pending += [b"I (123) main: noise\n", self.replies.pop(0).encode() + b"\n"]

    def readline(self):
        return self.pending.pop(0) if self.pending else b""

    def flush(self):
        pass


def make_zip(tmp_path, members):
    p = tmp_path / "set.zip"
    with zipfile.ZipFile(p, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
    return p


def test_zip_selects_known_roms(tmp_path, monkeypatch):
    monkeypatch.setattr(tool, "KNOWN", {FAKE_SHA: "P1"})
    z = make_zip(tmp_path, {"tama.bin": FAKE, "test.bin": b"\0" * 12288, "tama.svg": b"<svg/>"})
    roms = tool.roms_from_path(z)
    assert [(r.name, r.model) for r in roms] == [("tama.bin", "P1")]


def test_unknown_bin_skipped(tmp_path, monkeypatch):
    monkeypatch.setattr(tool, "KNOWN", {})
    p = tmp_path / "x.bin"
    p.write_bytes(FAKE)
    assert tool.roms_from_path(p) == []


def test_chunks_are_768_bytes_base64():
    chunks = list(tool.chunk_lines(FAKE))
    assert len(chunks) == 16
    assert chunks[0].startswith("@TO D 0 ")
    assert len(base64.b64decode(chunks[0].split(" ")[3])) == 768
    assert b"".join(base64.b64decode(c.split(" ")[3]) for c in chunks) == FAKE


def test_protocol_against_fake_port():
    replies = ["@TO OK"] + [f"@TO OK {i}" for i in range(16)] + ["@TO OK P1"]
    port = FakePort(replies)
    model = tool.Link(port, timeout=1).push(tool.Rom("tama.bin", FAKE, "P1"))
    assert model == "P1"
    assert port.written[0] == f"@TO PUT tama.bin 12288 {FAKE_SHA}"
    assert port.written[-1] == "@TO END"


def test_err_reply_raises():
    port = FakePort(["@TO ERR sha1"])
    with pytest.raises(tool.ToolError, match="sha1"):
        tool.Link(port, timeout=1).push(tool.Rom("tama.bin", FAKE, "P1"))


def test_wifi_line_is_base64():
    port = FakePort(["@TO OK"])
    tool.Link(port, timeout=1).wifi("Home Net", "p@ss word")
    _, cmd, ssid, pw = port.written[0].split(" ")
    assert cmd == "WIFI"
    assert base64.b64decode(ssid) == b"Home Net" and base64.b64decode(pw) == b"p@ss word"


def test_list_collects_items():
    port = FakePort(["@TO ITEM tama.bin P1 " + FAKE_SHA])
    port.replies.append("@TO OK")
    orig = port.write

    def write(data):  # LIST gets two replies for one request
        orig(data)
        port.pending.append(port.replies.pop(0).encode() + b"\n")

    port.write = write
    items = tool.Link(port, timeout=1).list()
    assert items == [("tama.bin", "P1", FAKE_SHA)]


def test_list_handles_spaces_in_names():
    port = FakePort(["@TO ITEM my tama.bin P1 " + FAKE_SHA])
    port.replies.append("@TO OK")
    orig = port.write

    def write(data):
        orig(data)
        port.pending.append(port.replies.pop(0).encode() + b"\n")

    port.write = write
    assert tool.Link(port, timeout=1).list() == [("my tama.bin", "P1", FAKE_SHA)]


def test_device_name_is_sanitised():
    assert tool.device_name("Tama P1 (dump).BIN") == "Tama_P1__dump_.bin"
    assert tool.device_name("tama.bin") == "tama.bin"
    assert tool.device_name("x" * 60 + ".bin") == "x" * 44 + ".bin"


def test_reboot_reply_is_reported():
    replies = ["@TO OK"] + [f"@TO OK {i}" for i in range(16)] + ["@TO OK P1 REBOOT"]
    link = tool.Link(FakePort(replies), timeout=1)
    assert link.push(tool.Rom("tama.bin", FAKE, "P1")) == "P1"
    assert link.rebooting


def test_open_port_does_not_reset_the_device(monkeypatch):
    """Measured on the ball: opening with DTR/RTS low resets the ESP32-S3; both high doesn't."""
    import sys
    import types

    seen = {}

    class FakeSerial:
        def open(self):
            seen["dtr"], seen["rts"] = self.dtr, self.rts

    monkeypatch.setitem(sys.modules, "serial", types.SimpleNamespace(Serial=FakeSerial))
    tool.open_port("/dev/null")
    assert seen == {"dtr": True, "rts": True}


def multi_port(replies):
    """FakePort that answers one request with several lines."""
    port = FakePort([])
    def write(data):
        port.written.append(data.decode().rstrip("\n"))
        port.pending += [b"I (1) x: noise\n"] + [r.encode() + b"\n" for r in replies]
    port.write = write
    return port


def test_get_prints_settings(monkeypatch, capsys):
    port = multi_port(["@TO SET brightness=80", "@TO OK"])
    monkeypatch.setattr(tool, "open_port", lambda name: port)
    assert tool.main(["get"]) == 0
    assert port.written == ["@TO GET"]
    assert capsys.readouterr().out == "brightness=80\n"


def test_set_sends_pairs(monkeypatch, capsys):
    port = multi_port(["@TO OK"])
    monkeypatch.setattr(tool, "open_port", lambda name: port)
    assert tool.main(["set", "volume=0", "palette=night"]) == 0
    assert port.written == ["@TO SET volume=0 palette=night"]
    assert capsys.readouterr().out == "ok\n"


def test_set_rejected_exit_code(monkeypatch, capsys):
    port = multi_port(["@TO ERR dim_level"])
    monkeypatch.setattr(tool, "open_port", lambda name: port)
    assert tool.main(["set", "dim_level=999"]) == 1
    assert "rejected: dim_level" in capsys.readouterr().err
