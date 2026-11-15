# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "tc_rom.c" "tc_sha1.c" "tama_core.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES tamalib)
