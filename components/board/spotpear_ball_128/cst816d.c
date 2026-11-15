// SPDX-License-Identifier: GPL-3.0-or-later
// CST816D touch controller, two points (register layout in docs/hardware/spotpear-ball-128.md).
#include "board.h"
#include "ball_internal.h"
#include "ball_pins.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_dev_handle_t s_dev;

esp_err_t ball_touch_init(void)
{
    gpio_set_direction(PIN_TP_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    i2c_master_bus_config_t bc = { .i2c_port = I2C_NUM_1, .sda_io_num = PIN_TP_SDA, .scl_io_num = PIN_TP_SCL,
                                   .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                   .flags.enable_internal_pullup = 1 };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &bus), "touch", "bus");
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = TP_ADDR, .scl_speed_hz = 400000 };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dc, &s_dev), "touch", "dev");
    const uint8_t no_sleep[2] = { 0xFE, 0x01 };
    i2c_master_transmit(s_dev, no_sleep, 2, 50);
    return ESP_OK;
}

static void point(const uint8_t *r, int16_t *x, int16_t *y)
{
    int raw_x = ((r[0] & 0x0F) << 8) | r[1];
    int raw_y = ((r[2] & 0x0F) << 8) | r[3];
    // Panel coordinates match the display after the driver's mirror (verified on hardware, Task 10).
    *x = (int16_t)(TOUCH_MIRROR_X ? 239 - raw_x : raw_x);
    *y = (int16_t)raw_y;
}

bool board_touch_read(board_touch_t *t)
{
    uint8_t reg = 0x00, r[13];
    t->count = 0;
    if (!s_dev || i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof r, 20) != ESP_OK) return false;
    int n = r[2] & 0x0F;
    if (n > 2) n = 2;
    for (int i = 0; i < n; i++) point(&r[3 + 6 * i], &t->pt[i].x, &t->pt[i].y);
    t->count = (uint8_t)n;
    return true;
}

bool board_boot_down(void) { return gpio_get_level(PIN_BOOT) == 0; }
