# SPDX-License-Identifier: GPL-3.0-or-later
# ui's icon tables live in its private ui_internal.h; only the ui directory is added, nothing is re-exported.
idf_component_register(
    SRCS "rm_look.c" "rm_png.c" "rm_press.c" "rm_save.c" "rm_json.c" "rm_mcp.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../ui"
    REQUIRES tama_core ui persist settings espressif__cjson)
