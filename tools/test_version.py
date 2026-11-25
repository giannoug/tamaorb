# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/version.py: the firmware's version string (short commit, plus -dirty)."""
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("version", os.path.join(HERE, "version.py"))
version_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version_mod)


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True, text=True).stdout.strip()


def make_repo(tmp_path):
    repo = str(tmp_path / "repo")
    os.mkdir(repo)
    git(repo, "init", "-q")
    git(repo, "config", "user.email", "t@example.invalid")
    git(repo, "config", "user.name", "t")
    with open(os.path.join(repo, "a.txt"), "w") as f:
        f.write("one\n")
    git(repo, "add", "a.txt")
    git(repo, "commit", "-q", "-m", "one")
    return repo


def test_clean_repo_is_short_sha(tmp_path):
    repo = make_repo(tmp_path)
    v = version_mod.version(repo)
    assert v == git(repo, "rev-parse", "--short=7", "HEAD")
    assert len(v) == 7


def test_modified_tracked_file_is_dirty(tmp_path):
    repo = make_repo(tmp_path)
    with open(os.path.join(repo, "a.txt"), "a") as f:
        f.write("two\n")
    assert version_mod.version(repo) == git(repo, "rev-parse", "--short=7", "HEAD") + "-dirty"


def test_untracked_file_is_not_dirty(tmp_path):
    repo = make_repo(tmp_path)
    with open(os.path.join(repo, "new.txt"), "w") as f:
        f.write("x\n")
    assert version_mod.version(repo) == git(repo, "rev-parse", "--short=7", "HEAD")


def test_no_git_is_unknown(tmp_path):
    plain = tmp_path / "plain"
    plain.mkdir()
    assert version_mod.version(str(plain)) == "unknown"


def test_cli_prints_version(tmp_path):
    repo = make_repo(tmp_path)
    out = subprocess.run([sys.executable, os.path.join(HERE, "version.py"), repo], check=True, capture_output=True,
                         text=True).stdout
    assert out == version_mod.version(repo)
