# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "web.c"
    INCLUDE_DIRS "include"
    REQUIRES remote
    PRIV_REQUIRES esp_http_server esp_netif esp_timer heap log espressif__cjson espressif__mdns net settings tama_core)
