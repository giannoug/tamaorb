# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "hl_proto.c" "hl_base64.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES tama_core)
