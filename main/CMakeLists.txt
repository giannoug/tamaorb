# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "rom_store.c" "emu_task.c" "ui_task.c" "saver.c" "hostlink_task.c" "settings_store.c"
                            "rom_switch.c" "press_task.c" "remote_ops.c" "qr_encode.c" "pet_clock.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "guide_p1.md" "guide_p2.md"
                       REQUIRES settings remote web tamalib tama_core ui persist board net hostlink power nvs_flash
                                esp_driver_usb_serial_jtag esp_timer esp_app_format esp_wifi)
