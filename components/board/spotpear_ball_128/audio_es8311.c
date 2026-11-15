// SPDX-License-Identifier: GPL-3.0-or-later
// ES8311 + speaker amp: a square wave at the ROM's buzzer frequency (spec §5.6).
#include "board.h"
#include "ball_internal.h"
#include "ball_pins.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RATE       32000
#define CHUNK      320   // 10 ms
#define AMPLITUDE  8000

static const char *TAG = "audio";
static esp_codec_dev_handle_t s_dev;
static TaskHandle_t s_task;
static volatile uint32_t s_freq_hz;
static volatile bool s_on;
static volatile bool s_muted;

static void amp(bool on) { gpio_set_level(PIN_AMP_EN, on); }

static void writer(void *arg)
{
    static int16_t buf[CHUNK];
    uint32_t phase = 0;  // 16.16 fixed point position within one period
    for (;;) {
        if (!s_on || s_freq_hz == 0 || s_muted) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        uint32_t step = (uint32_t)(((uint64_t)s_freq_hz << 16) / RATE);
        for (int i = 0; i < CHUNK; i++) {
            buf[i] = (phase & 0x8000) ? AMPLITUDE : -AMPLITUDE;
            phase = (phase + step) & 0xFFFF;
        }
        esp_codec_dev_write(s_dev, buf, sizeof buf);  // paced by the I2S DMA
    }
}

esp_err_t ball_audio_init(void)
{
    i2c_master_bus_config_t bc = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_CODEC_SDA, .scl_io_num = PIN_CODEC_SCL,
                                   .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                   .flags.enable_internal_pullup = 1 };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &bus), TAG, "i2c");

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;  // underflow plays silence
    i2s_chan_handle_t tx;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &tx, NULL), TAG, "i2s chan");
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS, .dout = PIN_I2S_DOUT, .din = I2S_GPIO_UNUSED },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "i2s enable");

    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus };
    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = tx };
    es8311_codec_cfg_t es = {
        .ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg),
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,  // we drive the amp ourselves so it is off while silent
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    esp_codec_dev_cfg_t dev_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = es8311_codec_new(&es),
                                    .data_if = audio_codec_new_i2s_data(&i2s_cfg) };
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) return ESP_FAIL;
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = RATE };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    // While unmuted the amp stays on: switching it on per beep swallowed the ROM's ~54 ms key
    // clicks (measured). Between sounds the I2S line carries digital silence (auto_clear).
    board_volume_set(CONFIG_TAMAORB_VOLUME);
    xTaskCreatePinnedToCore(writer, "audio", 3072, NULL, 8, &s_task, 0);
    return ESP_OK;
}

void board_tone(uint32_t freq_hz, bool on)
{
    s_freq_hz = freq_hz;
    s_on = on;
    if (s_task) xTaskNotifyGive(s_task);
}

void board_volume_set(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_muted = pct == 0;
    amp(!s_muted);  // muted: amp off, no hum
    if (s_dev && pct) esp_codec_dev_set_out_vol(s_dev, pct);
}
