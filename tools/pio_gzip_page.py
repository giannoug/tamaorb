# SPDX-License-Identifier: GPL-3.0-or-later
# PlatformIO pre-build script: gzip the web panel page into the build tree before every build.
# components/web/CMakeLists.txt also gzips it, but only when CMake configures, and PlatformIO doesn't
# re-run that step when just the page changes; without this an edited page would never reach the image.
# Python's gzip, not a gzip program, so it works on Windows too.
import gzip
import os

Import("env")  # noqa: F821 (provided by PlatformIO's SCons)

src = os.path.join(env.subst("$PROJECT_DIR"), "components", "web", "page", "index.html")  # noqa: F821
dst_dir = os.path.join(env.subst("$BUILD_DIR"), "esp-idf", "web")  # noqa: F821
dst = os.path.join(dst_dir, "index.html.gz")
with open(src, "rb") as f:
    data = gzip.compress(f.read(), compresslevel=9, mtime=0)
os.makedirs(dst_dir, exist_ok=True)
old = open(dst, "rb").read() if os.path.exists(dst) else None
if old is None or gzip.decompress(old) != gzip.decompress(data):  # leave an up-to-date file alone (no rebuild)
    with open(dst, "wb") as f:
        f.write(data)
