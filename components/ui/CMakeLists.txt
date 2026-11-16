# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "ui_palette.c" "ui_pet.c" "ui_icons.c" "ui_font.c" "ui_touch.c" "ui_screen.c" "ui_text.c" "ui_geom.c" "ui_gesture.c" "ui_qr.c" "ui_glance.c" "ui_menu.c" "ui_nav.c" "ui_send.c" "third_party/qrcodegen/qrcodegen.c"
    INCLUDE_DIRS "include"
    REQUIRES tama_core settings tzdb)
target_include_directories(${COMPONENT_LIB} PRIVATE "third_party/qrcodegen")
