# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "rom_store.c" "emu_task.c" "ui_task.c" "saver.c" "hostlink_task.c" "settings_store.c" INCLUDE_DIRS "." REQUIRES settings remote tamalib tama_core ui persist board net hostlink power nvs_flash esp_driver_usb_serial_jtag esp_timer esp_app_format)
