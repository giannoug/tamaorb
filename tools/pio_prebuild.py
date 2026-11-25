# SPDX-License-Identifier: GPL-3.0-or-later
# PlatformIO pre-build script (platformio.ini extra_scripts). Two gaps in PlatformIO's ESP-IDF builder:
#
# 1. It re-runs CMake only when the root or main/ CMakeLists.txt, sdkconfig(.defaults) or dependencies.lock
#    change, not when a component's CMakeLists.txt, idf_component.yml or Kconfig does: after pulling such a
#    change, an incremental build would use the old file lists. If any of those is newer than build.ninja,
#    remove build.ninja, which makes PlatformIO configure again.
# 2. components/web/CMakeLists.txt gzips the web pages while CMake configures, and an edited page alone doesn't
#    trigger that. Gzip them here before every build (Python's gzip: works on Windows too), writing only when the
#    content changed so an unchanged page doesn't relink.
# 3. The firmware version (tools/version.py: the commit, plus "-dirty") is set while CMake configures, so a new
#    commit or a newly dirty tree wouldn't reach the build. If it differs from what the last configure wrote to
#    tamaorb_version.txt, remove build.ninja so PlatformIO configures again.
import glob
import gzip
import importlib.util
import os

Import("env")  # noqa: F821 (provided by PlatformIO's SCons)

project = env.subst("$PROJECT_DIR")  # noqa: F821
build = env.subst("$BUILD_DIR")  # noqa: F821

ninja = os.path.join(build, "build.ninja")
if os.path.isfile(ninja):
    built = os.path.getmtime(ninja)
    watched = []
    for pattern in ("CMakeLists.txt", "idf_component.yml", "Kconfig*"):
        watched += glob.glob(os.path.join(project, "components", "**", pattern), recursive=True)
    watched += glob.glob(os.path.join(project, "main", "idf_component.yml"))
    if any(os.path.getmtime(f) > built for f in watched):
        os.remove(ninja)

stamp = os.path.join(build, "tamaorb_version.txt")
if os.path.isfile(ninja) and os.path.isfile(stamp):
    _spec = importlib.util.spec_from_file_location("tamaorb_version", os.path.join(project, "tools", "version.py"))
    _version = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_version)
    with open(stamp) as f:
        configured = f.read()
    if configured != _version.version(project):
        os.remove(ninja)

dst_dir = os.path.join(build, "esp-idf", "web")
os.makedirs(dst_dir, exist_ok=True)
for page in ("index.html", "debug.html"):  # the panel and the debugger (components/web/CMakeLists.txt)
    src = os.path.join(project, "components", "web", "page", page)
    dst = os.path.join(dst_dir, page + ".gz")
    with open(src, "rb") as f:
        data = gzip.compress(f.read(), compresslevel=9, mtime=0)
    old = open(dst, "rb").read() if os.path.exists(dst) else None
    # Same page, same bytes (mtime=0, fixed level), so compare them directly: decompressing the old file crashed
    # the build when an interrupted one had left it truncated.
    if old != data:
        with open(dst, "wb") as f:
            f.write(data)
