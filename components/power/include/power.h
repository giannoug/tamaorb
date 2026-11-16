// SPDX-License-Identifier: GPL-3.0-or-later
// Battery gauge, low-battery deep sleep and light sleep.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define POWER_LOW_MV          3400
#define POWER_LOW_READS       3       // consecutive 10 s readings below the threshold
#define POWER_WAKE_CHECK_S    60

// First thing in app_main: after a low-battery timer wake with USB still absent, sleep again.
void    power_early_boot_check(void);
// Enables automatic light sleep (taken only while nothing holds a no-sleep lock) and starts the
// battery monitor. On critical battery: save_now(), show_low(), wait 3 s, then deep sleep.
void    power_start(void (*save_now)(void), void (*show_low)(void));
// While the screen is on, light sleep would stop the backlight PWM: hold this lock meanwhile.
void    power_screen_on(bool on);
uint8_t power_battery_pct(uint32_t mv);
