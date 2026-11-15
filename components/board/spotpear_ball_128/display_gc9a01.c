// SPDX-License-Identifier: GPL-3.0-or-later
// GC9A01A over SPI with xiaozhi's extra init registers (spec §3). Frames go out in DMA bands.
#include <string.h>
#include "board.h"
#include "ball_internal.h"
#include "ball_pins.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define W 240
#define H 240
#define BAND_ROWS 20
#define SPI_HZ (40 * 1000 * 1000)

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_band[2];
static SemaphoreHandle_t s_band_free;  // counts band buffers not owned by the DMA

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_band_free, &woken);
    return woken == pdTRUE;
}

static void tx(uint8_t cmd, const uint8_t *data, size_t n) { esp_lcd_panel_io_tx_param(s_io, cmd, data, n); }

esp_err_t ball_display_init(void)
{
    spi_bus_config_t bus = GC9A01_PANEL_BUS_SPI_CONFIG(PIN_LCD_SCLK, PIN_LCD_MOSI, W * BAND_ROWS * 2);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO), "disp", "spi bus");

    s_band_free = xSemaphoreCreateCounting(2, 2);
    esp_lcd_panel_io_spi_config_t io_cfg = GC9A01_PANEL_IO_SPI_CONFIG(PIN_LCD_CS, PIN_LCD_DC, on_trans_done, NULL);
    io_cfg.pclk_hz = SPI_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_cfg, &s_io), "disp", "panel io");

    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(s_io, &dev, &s_panel), "disp", "panel");
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, false);

    // Extra registers sent by xiaozhi's sp-esp32-s3-1.28-box board.
    tx(0x62, (const uint8_t[]){ 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70 }, 12);
    tx(0x63, (const uint8_t[]){ 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70 }, 12);
    tx(0x36, (const uint8_t[]){ 0x48 }, 1);
    tx(0xC3, (const uint8_t[]){ 0x1F }, 1);
    tx(0xC4, (const uint8_t[]){ 0x1F }, 1);

    for (int i = 0; i < 2; i++) {
        s_band[i] = heap_caps_malloc(W * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_band[i]) return ESP_ERR_NO_MEM;
    }
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

void board_display_flush(const uint16_t *fb)
{
    for (int y = 0, b = 0; y < H; y += BAND_ROWS, b ^= 1) {
        xSemaphoreTake(s_band_free, portMAX_DELAY);
        const uint16_t *src = fb + y * W;
        uint16_t *dst = s_band[b];
        for (int i = 0; i < W * BAND_ROWS; i++) dst[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));  // panel wants big-endian
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, W, y + BAND_ROWS, dst);
    }
    // Wait for both bands to come back so the caller may reuse fb immediately.
    xSemaphoreTake(s_band_free, portMAX_DELAY);
    xSemaphoreTake(s_band_free, portMAX_DELAY);
    xSemaphoreGive(s_band_free);
    xSemaphoreGive(s_band_free);
}
