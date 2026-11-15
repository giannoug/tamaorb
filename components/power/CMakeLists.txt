# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "power.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board esp_pm esp_hw_support esp_wifi log freertos)
