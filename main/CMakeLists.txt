# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "rom_store.c" INCLUDE_DIRS "." REQUIRES tamalib tama_core ui persist board nvs_flash esp_app_format)
