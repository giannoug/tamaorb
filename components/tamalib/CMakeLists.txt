# SPDX-License-Identifier: GPL-3.0-or-later
# tamalib (GPL-2.0-or-later) is used unmodified from the submodule: the owner's fork (perf-and-fixes) until its
# fixes land in jcrona/tamalib.
idf_component_register(
    SRCS "upstream/cpu.c" "upstream/hw.c" "upstream/tamalib.c"
    INCLUDE_DIRS "." "upstream")
# Opcode lookup table: ~2.7x faster stepping for 4 KB of DRAM, built in tamalib_init().
target_compile_definitions(${COMPONENT_LIB} PUBLIC OP_LOOKUP_TABLE)
# Upstream callbacks have unused arguments.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
