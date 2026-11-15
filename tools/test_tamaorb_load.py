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
