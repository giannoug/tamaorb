#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Move Tamagotchi ROMs to a TamaOrb over USB, and set it up.

  tamaorb-rom.py push roms/tama.zip roms/tamag2.zip   # MAME zips or raw .bin dumps
  tamaorb-rom.py list
  tamaorb-rom.py remove tama.bin
  tamaorb-rom.py wifi "My Network"                    # prompts for the password
  tamaorb-rom.py status
  tamaorb-rom.py get                                  # all settings as key=value
  tamaorb-rom.py set brightness=30 palette=night      # all or nothing

Talks the "@TO" line protocol (docs: components/hostlink/include/hostlink.h) over the same
USB serial port used for flashing. Only P1 and P2 dumps are sent; nothing else leaves the PC.
"""
import argparse
import base64
import getpass
import hashlib
import pathlib
import re
import sys
import time
import zipfile
from dataclasses import dataclass

# SHA-1 of the supported dumps (MAME src/mame/handheld/hh_e0c6200.cpp).
KNOWN = {
    "4b4979cf92dc9d2fb6d7295a38f209f3da144f72": "P1",
    "09e5101b37636a314fc599d5d69b4846721b3c88": "P2",
}
ROM_BYTES = 12288
CHUNK = 768
# Every key "@TO GET" lists (components/settings/st_settings.c); a listing missing any is incomplete.
SETTINGS_KEYS = ("brightness", "dim_level", "dim_usb_s", "dim_battery_s", "off_battery_s", "volume", "palette",
                 "name", "ntp", "agent_feedback")


class ToolError(Exception):
    pass


@dataclass
class Rom:
    name: str
    data: bytes
    model: str


def device_name(name):
    """A name the device accepts: [A-Za-z0-9._-], ending in lowercase .bin, at most 48 chars."""
    stem = pathlib.PurePath(name).name
    if stem.lower().endswith(".bin"):
        stem = stem[:-4]
    stem = re.sub(r"[^A-Za-z0-9._-]", "_", stem).lstrip(".") or "rom"
    return stem[:44] + ".bin"


def _identify(name, data):
    sha = hashlib.sha1(data).hexdigest()
    model = KNOWN.get(sha) if len(data) == ROM_BYTES else None
    if not model:
        print(f"  skipping {name}: not a supported P1/P2 dump (sha1 {sha[:10]}...)")
        return None
    clean = device_name(name)
    if clean != pathlib.PurePath(name).name:
        print(f"  {pathlib.PurePath(name).name} will be stored as {clean}")
    return Rom(clean, data, model)


def roms_from_path(path):
    """Supported ROMs in a .bin file or a MAME .zip (test ROMs and artwork are skipped)."""
    path = pathlib.Path(path)
    found = []
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.filename.lower().endswith(".bin") and info.file_size == ROM_BYTES:
                    rom = _identify(info.filename, z.read(info))
                    if rom:
                        found.append(rom)
                elif not info.is_dir():
                    print(f"  skipping {info.filename}")
    else:
        rom = _identify(path.name, path.read_bytes())
        if rom:
            found.append(rom)
    return found


def chunk_lines(data):
    for seq, off in enumerate(range(0, len(data), CHUNK)):
        yield f"@TO D {seq} " + base64.b64encode(data[off:off + CHUNK]).decode()


class Link:
    def __init__(self, port, timeout=5.0):
        self.port = port
        self.timeout = timeout
        self.rebooting = False
        self._buf = b""

    def _send(self, line):
        # Every reply to the previous request has been read by now: anything waiting is log output or a
        # leftover from an interrupted run, and must not be taken for this request's reply.
        self.port.reset_input_buffer()
        self._buf = b""
        self.port.write((line + "\n").encode())
        self.port.flush()

    def _line(self, deadline):
        """Next whole line, or None at the deadline. readline() returns a partial line when the port's
        timeout expires mid-line, so the pieces are joined until the newline arrives."""
        while True:
            nl = self._buf.find(b"\n")
            if nl >= 0:
                line, self._buf = self._buf[:nl], self._buf[nl + 1:]
                return line.decode(errors="replace").strip()
            if time.monotonic() >= deadline:
                return None
            self._buf += self.port.readline()

    def _reply(self):
        """Next protocol line; log output from the device is skipped."""
        deadline = time.monotonic() + self.timeout
        while (line := self._line(deadline)) is not None:
            if line.startswith("@TO ERR"):
                raise ToolError(f"device error: {line[8:]}")
            if line.startswith("@TO "):
                return line
        raise ToolError("no reply from the device (is TamaOrb running on this port?)")

    def _expect(self, want):
        got = self._reply()
        if got != want:
            raise ToolError(f"unexpected reply: {got!r} (wanted {want!r})")

    def push(self, rom):
        self._send(f"@TO PUT {rom.name} {len(rom.data)} {hashlib.sha1(rom.data).hexdigest()}")
        self._expect("@TO OK")
        for seq, line in enumerate(chunk_lines(rom.data)):
            self._send(line)
            self._expect(f"@TO OK {seq}")
        self._send("@TO END")
        got = self._reply()
        if not got.startswith("@TO OK "):
            raise ToolError(f"unexpected reply: {got!r}")
        model, _, flag = got[7:].partition(" ")
        self.rebooting = flag == "REBOOT"
        return model

    def list(self):
        self._send("@TO LIST")
        items = []
        while True:
            line = self._reply()
            if line == "@TO OK":
                return items
            if line.startswith("@TO ITEM "):
                name, model, sha = line[9:].rsplit(" ", 2)  # SD file names may contain spaces
                items.append((name, model, sha))

    def remove(self, name):
        self._send(f"@TO RM {name}")
        self._expect("@TO OK")

    def wifi(self, ssid, password):
        b = lambda s: base64.b64encode(s.encode()).decode()
        self._send(f"@TO WIFI {b(ssid)} {b(password)}" if password else f"@TO WIFI {b(ssid)}")
        self._expect("@TO OK")

    def _get_once(self):
        self._send("@TO GET")
        pairs = []
        while True:
            line = self._reply()
            if line == "@TO OK":
                return pairs
            if line.startswith("@TO SET "):
                pairs.append(line[8:])

    def get(self):
        """All settings as "key=value"; asks twice before giving up on a listing with keys missing."""
        for _ in range(2):
            pairs = self._get_once()
            missing = [k for k in SETTINGS_KEYS if k not in {p.partition("=")[0] for p in pairs}]
            if not missing:
                return pairs
        raise ToolError(f"incomplete settings listing from the device (missing {', '.join(missing)})")

    def set(self, pairs):
        self._send("@TO SET " + " ".join(pairs))
        try:
            self._expect("@TO OK")
        except ToolError as e:
            msg = str(e)
            if msg.startswith("device error: ") and msg[14:] != "data":
                raise ToolError(f"rejected: {msg[14:]}") from None
            raise

    def status(self):
        """Asks twice: right after a boot the first request can go unanswered (seen once on the ball)."""
        for attempt in range(2):
            self._send("@TO STATUS")
            try:
                return self._reply()[11:]
            except ToolError as e:
                if attempt or not str(e).startswith("no reply"):
                    raise


def open_port(name):
    import serial  # pyserial; imported here so the tests don't need a port

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = name, 115200, 0.2
    # Linux raises DTR and RTS on open; dropping DTR while RTS is high resets the ESP32-S3's
    # USB-Serial/JTAG (measured). Keeping both high leaves the ball running.
    s.dtr = s.rts = True
    s.open()
    return s


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("push", help="send ROMs (.bin or MAME .zip)")
    p.add_argument("files", nargs="+")
    sub.add_parser("list", help="list ROMs on the device's SD card")
    r = sub.add_parser("remove", help="delete a ROM from the SD card")
    r.add_argument("name")
    w = sub.add_parser("wifi", help="set Wi-Fi credentials (the device reboots)")
    w.add_argument("ssid")
    w.add_argument("--password")
    sub.add_parser("status", help="show device status")
    sub.add_parser("get", help="print the settings as key=value")
    st = sub.add_parser("set", help="change settings, e.g. brightness=30 (all or nothing)")
    st.add_argument("pairs", nargs="+", metavar="key=value")
    args = ap.parse_args(argv)

    try:
        if args.cmd == "push":
            roms = [rom for f in args.files for rom in roms_from_path(f)]
            if not roms:
                raise ToolError("no supported ROMs found")
        link = Link(open_port(args.port))
        if args.cmd == "push":
            for i, rom in enumerate(roms):
                print(f"{rom.name} ({rom.model}): ", end="", flush=True)
                print(f"written ({link.push(rom)})")
                if link.rebooting:
                    print("no SD card: this ROM is now active and the device is rebooting")
                    if i + 1 < len(roms):
                        print("  (without an SD card only one ROM is kept; the rest were not sent)")
                    break
        elif args.cmd == "list":
            for name, model, sha in link.list():
                print(f"{name:24} {model:8} {sha}")
        elif args.cmd == "remove":
            link.remove(args.name)
            print(f"removed {args.name}")
        elif args.cmd == "wifi":
            password = args.password if args.password is not None else getpass.getpass("Password (empty for open): ")
            link.wifi(args.ssid, password)
            print("saved; the device is rebooting to connect")
        elif args.cmd == "status":
            print(link.status())
        elif args.cmd == "get":
            print("\n".join(link.get()))
        elif args.cmd == "set":
            link.set(args.pairs)
            print("ok")
    except (ToolError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
