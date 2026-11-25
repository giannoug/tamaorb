# SPDX-License-Identifier: GPL-3.0-or-later
"""The firmware's version string: the 7-character commit, plus "-dirty" when tracked files have uncommitted
changes (untracked files don't count); "unknown" outside a git checkout.

Used by the root CMakeLists.txt (PROJECT_VER, at configure) and tools/pio_prebuild.py (which reconfigures
when the version changed). Standard library only: it runs under ESP-IDF's and PlatformIO's Pythons.

    python3 tools/version.py <repo>   # prints the version, no newline
"""
import subprocess
import sys


def _git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True)


def version(repo):
    try:
        head = _git(repo, "rev-parse", "--short=7", "HEAD")
    except OSError:  # no git installed
        return "unknown"
    if head.returncode != 0:
        return "unknown"
    sha = head.stdout.strip()
    dirty = _git(repo, "diff", "--quiet", "HEAD", "--").returncode != 0
    return sha + "-dirty" if dirty else sha


if __name__ == "__main__":
    sys.stdout.write(version(sys.argv[1] if len(sys.argv) > 1 else "."))
