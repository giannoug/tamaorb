# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tamaorb-load.py's request bodies (no network)."""
import importlib.util
import json
import pathlib

_spec = importlib.util.spec_from_file_location("tamaorb_load", pathlib.Path(__file__).with_name("tamaorb-load.py"))
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)


def test_padded_patch_is_valid_and_exactly_at_the_cap():
    body = tool.padded_patch("agent_feedback", "quiet")
    assert len(body) == tool.BODY_MAX == 16384
    assert json.loads(body) == {"agent_feedback": "quiet"}


def test_mcp_look_request():
    req = json.loads(tool.MCP_LOOK)
    assert req["method"] == "tools/call" and req["params"]["name"] == "look"


def test_exit_codes(monkeypatch, capsys):
    # A fake ball: every request succeeds; heap_min is set per case.
    state = {"heap_min": 30000}

    def fake_call(base, method, path, body=None, timeout=15):
        if path == "/api/status":
            return 200, json.dumps({"heap_min": state["heap_min"], "heap_free": 80000, "uptime_s": 1}).encode()
        if path == "/api/look":
            return 200, b'{"text":"x","png":"y"}'
        return 200, json.dumps({"result": {"content": [{"type": "text"}, {"type": "image"}]}}).encode()

    monkeypatch.setattr(tool, "call", fake_call)
    assert tool.main(["--seconds", "0.05", "--workers", "1"]) == 0
    state["heap_min"] = tool.HEAP_MIN_FLOOR - 1
    assert tool.main(["--seconds", "0.05", "--workers", "1"]) == 1


def test_large_setup_unreachable_is_a_failure(monkeypatch, capsys):
    def boom(*a, **k):
        raise OSError("unreachable")

    monkeypatch.setattr(tool, "call", boom)
    assert tool.main(["--large", "--seconds", "0.05"]) == 1
    assert "requests=1 failures=1" in capsys.readouterr().out
