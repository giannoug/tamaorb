# SPDX-License-Identifier: GPL-3.0-or-later
# tamalib (GPL-2.0-or-later) is used unmodified from the upstream submodule.
idf_component_register(
    SRCS "upstream/cpu.c" "upstream/hw.c" "upstream/tamalib.c"
    INCLUDE_DIRS "." "upstream")
# Upstream reads an uninitialised local in cpu_step() while halted.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=maybe-uninitialized -Wno-unused-parameter)
