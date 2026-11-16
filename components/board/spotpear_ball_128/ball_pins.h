// SPDX-License-Identifier: GPL-3.0-or-later
// SpotPear 1.28" ball pin map (docs/hardware/spotpear-ball-128.md).
#pragma once

#define PIN_POWER_HOLD  3    // not a latch on the measured unit, driven high anyway
#define PIN_BOOT        0
#define PIN_USB_SENSE   41
#define PIN_BAT_ADC     1    // ADC1_CH0, divider x2

#define PIN_LCD_SCLK    4
#define PIN_LCD_MOSI    2
#define PIN_LCD_CS      5
#define PIN_LCD_DC      47
#define PIN_LCD_RST     38
#define PIN_LCD_BL      42   // active low

#define PIN_TP_SDA      11
#define PIN_TP_SCL      7
#define PIN_TP_RST      6
#define PIN_TP_INT      12
#define TP_ADDR         0x15

#define PIN_CODEC_SDA   15
#define PIN_CODEC_SCL   14
#define PIN_I2S_MCLK    16
#define PIN_I2S_BCLK    9
#define PIN_I2S_WS      45
#define PIN_I2S_DOUT    8
#define PIN_I2S_DIN     10
#define PIN_AMP_EN      46

#define PIN_SD_CLK      17
#define PIN_SD_CMD      18
#define PIN_SD_D0       21
