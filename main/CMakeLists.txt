# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "." REQUIRES tamalib tama_core ui persist esp_app_format)
