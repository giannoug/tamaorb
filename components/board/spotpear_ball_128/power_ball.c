// SPDX-License-Identifier: GPL-3.0-or-later
// Battery voltage (GPIO1, divider x2) and USB presence (GPIO41).
#include "board.h"
#include "ball_pins.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

static bool s_usb_gpio;

static void usb_gpio_init(void)
{
    if (s_usb_gpio) return;
    gpio_config_t usb = { .pin_bit_mask = 1ULL << PIN_USB_SENSE, .mode = GPIO_MODE_INPUT };
    gpio_config(&usb);
    s_usb_gpio = true;
}

esp_err_t ball_power_init(void)
{
    usb_gpio_init();
    adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_1 };
    esp_err_t err = adc_oneshot_new_unit(&uc, &s_adc);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &cc);
    adc_cali_curve_fitting_config_t cf = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
                                           .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_curve_fitting(&cf, &s_cali) != ESP_OK) s_cali = NULL;
    return ESP_OK;
}

uint32_t board_battery_mv(void)
{
    if (!s_adc) return 0;
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0, mv = 0;
        adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw);
        if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv);
        else mv = raw * 3100 / 4095;
        sum += mv;
    }
    return (uint32_t)(sum / 8) * 2;  // resistor divider halves the cell voltage
}

bool board_usb_present(void)  // usable before board_init (early low-battery check)
{
    usb_gpio_init();
    return gpio_get_level(PIN_USB_SENSE) == 1;
}
