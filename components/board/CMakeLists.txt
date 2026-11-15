# SPDX-License-Identifier: GPL-3.0-or-later
set(srcs)
if(CONFIG_TAMAORB_BOARD_SPOTPEAR_BALL_128)
    list(APPEND srcs "spotpear_ball_128/board_ball.c" "spotpear_ball_128/display_gc9a01.c" "spotpear_ball_128/cst816d.c" "spotpear_ball_128/sd_ball.c" "spotpear_ball_128/audio_es8311.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "spotpear_ball_128"
    REQUIRES esp_common
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_ledc esp_driver_i2c esp_driver_sdmmc esp_driver_i2s sdmmc fatfs esp_lcd freertos log)
