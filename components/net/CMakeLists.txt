# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "net.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_system log freertos lwip)
