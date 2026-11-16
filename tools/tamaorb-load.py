#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Load test for the TamaOrb web API.

  tamaorb-load.py [--host tamaorb.local] [--seconds 180] [--workers 4] [--large]

Each worker thread loops over GET /api/status, GET /api/look and POST /mcp (tools/call look).
--large adds two 16 KB-class bodies per loop: a PATCH /api/settings padded with whitespace to
exactly 16384 bytes (the cap; it re-sends a current value, so nothing changes) and a 16385-byte
PUT of a save, which must get 413. Prints requests=<n> failures=<n> and the ball's heap_min,
and exits 1 if any request failed or heap_min is under 20 KB. Pace is checked in the
serial log. Standard library only.
"""
import argparse
import json
import sys
import threading
import time
import urllib.error
import urllib.request

BODY_MAX = 16384
HEAP_MIN_FLOOR = 20480  # internal RAM floor (ADR 0005)
MCP_LOOK = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": "look", "arguments": {}}}).encode()


def call(base, method, path, body=None, timeout=15):
    """Returns (HTTP status, body bytes). Network errors raise."""
    req = urllib.request.Request(base + path, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json" if method != "PUT" else "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def padded_patch(key, value):
    """A valid settings patch padded with spaces to exactly BODY_MAX bytes."""
    core = json.dumps({key: value})
    return (core[:-1] + " " * (BODY_MAX - len(core)) + "}").encode()


def requests_for(base, large):
    """The loop each worker runs: (method, path, body, expected status, check)."""
    def look_ok(b):
        j = json.loads(b)
        return "text" in j and "png" in j

    def mcp_ok(b):
        j = json.loads(b)
        return [c["type"] for c in j["result"]["content"]] == ["text", "image"]

    loop = [
        ("GET", "/api/status", None, 200, lambda b: "heap_min" in json.loads(b)),
        ("GET", "/api/look", None, 200, look_ok),
        ("POST", "/mcp", MCP_LOOK, 200, mcp_ok),
    ]
    if large:
        _, b = call(base, "GET", "/api/settings")
        fb = json.loads(b)["settings"]["agent_feedback"]
        loop += [
            ("PATCH", "/api/settings", padded_patch("agent_feedback", fb), 200,
             lambda b: json.loads(b)["settings"]["agent_feedback"] == fb),
            ("PUT", "/api/pets/" + "0" * 40 + "/save?mode=frozen", b"\0" * (BODY_MAX + 1), 413, None),
        ]
    return loop


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="tamaorb.local")
    ap.add_argument("--seconds", type=float, default=180)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--large", action="store_true", help="add the 16384-byte PATCH and the 16385-byte PUT (413)")
    a = ap.parse_args(argv)
    base = "http://" + a.host
    try:
        loop = requests_for(base, a.large)
    except Exception as e:  # --large reads the current settings first
        print("fail GET /api/settings: %s: %s" % (type(e).__name__, e), file=sys.stderr)
        print("requests=1 failures=1")
        return 1

    lock = threading.Lock()
    counts = {"requests": 0, "failures": 0}
    end = time.monotonic() + a.seconds

    def worker(w):
        i = w  # stagger the workers through the loop
        while time.monotonic() < end:
            method, path, body, want, check = loop[i % len(loop)]
            i += 1
            why = None
            try:
                status, data = call(base, method, path, body)
                if status != want:
                    why = "status %d" % status
                elif check and not check(data):
                    why = "bad body"
            except Exception as e:  # timeouts, resets, refused connections, bad JSON
                why = "%s: %s" % (type(e).__name__, e)
            with lock:
                counts["requests"] += 1
                if why:
                    counts["failures"] += 1
                    print("fail %s %s: %s" % (method, path, why), file=sys.stderr)

    threads = [threading.Thread(target=worker, args=(w,), daemon=True) for w in range(a.workers)]
    t0 = time.monotonic()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    took = time.monotonic() - t0
    print("requests=%d failures=%d" % (counts["requests"], counts["failures"]))
    print("%.1f req/s over %.0f s with %d workers" % (counts["requests"] / took, took, a.workers))
    try:
        _, b = call(base, "GET", "/api/status")
        st = json.loads(b)
        print("heap_min=%d heap_free=%d uptime_s=%d" % (st["heap_min"], st["heap_free"], st["uptime_s"]))
    except Exception as e:
        print("status after the run failed: %s" % e, file=sys.stderr)
        return 1
    if st["heap_min"] < HEAP_MIN_FLOOR:
        print("heap_min %d is under the %d floor" % (st["heap_min"], HEAP_MIN_FLOOR), file=sys.stderr)
        return 1
    return 1 if counts["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
