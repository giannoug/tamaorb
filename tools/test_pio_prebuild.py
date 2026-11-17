# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/pio_prebuild.py runs inside PlatformIO's SCons; here it gets a stand-in env."""
import gzip
import pathlib
import runpy

SCRIPT = pathlib.Path(__file__).with_name("pio_prebuild.py")


class Env:
    def __init__(self, project, build):
        self.vars = {"$PROJECT_DIR": str(project), "$BUILD_DIR": str(build)}

    def subst(self, s):
        return self.vars[s]


def run(tmp_path):
    project, build = tmp_path / "proj", tmp_path / "build"
    env = Env(project, build)
    runpy.run_path(str(SCRIPT), init_globals={"Import": lambda name: None, "env": env})
    return build / "esp-idf" / "web" / "index.html.gz"


def make_page(tmp_path, html=b"<html>panel</html>"):
    page = tmp_path / "proj" / "components" / "web" / "page" / "index.html"
    page.parent.mkdir(parents=True, exist_ok=True)
    page.write_bytes(html)
    return page


def test_writes_the_gzipped_page(tmp_path):
    make_page(tmp_path)
    out = run(tmp_path)
    assert gzip.decompress(out.read_bytes()) == b"<html>panel</html>"


def test_truncated_gz_from_an_interrupted_build_is_replaced(tmp_path):
    make_page(tmp_path)
    out = run(tmp_path)
    out.write_bytes(out.read_bytes()[:10])  # a build cut off while writing it
    run(tmp_path)
    assert gzip.decompress(out.read_bytes()) == b"<html>panel</html>"


def test_unchanged_page_is_not_rewritten(tmp_path):
    make_page(tmp_path)
    out = run(tmp_path)
    before = out.stat().st_mtime_ns
    run(tmp_path)
    assert out.stat().st_mtime_ns == before  # no relink for an unchanged page


def test_changed_page_is_rewritten(tmp_path):
    page = make_page(tmp_path)
    out = run(tmp_path)
    page.write_bytes(b"<html>new</html>")
    run(tmp_path)
    assert gzip.decompress(out.read_bytes()) == b"<html>new</html>"
