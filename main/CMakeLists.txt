# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "rom_store.c" "emu_task.c" "ui_task.c" "saver.c" INCLUDE_DIRS "." REQUIRES tamalib tama_core ui persist board net nvs_flash esp_timer esp_app_format)
