# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "ui_palette.c" "ui_pet.c" "ui_icons.c" "ui_font.c" "ui_touch.c" "ui_screen.c" "ui_text.c" "ui_geom.c" "ui_gesture.c"
    INCLUDE_DIRS "include"
    REQUIRES tama_core)
