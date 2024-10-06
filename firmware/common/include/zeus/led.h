// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int led_boot(void);

int led_battery_charging(void);

int led_battery_full(void);

int led_battery_discharging(void);

int led_sync_started(void);

int led_sync_terminated(void);

int led_record_waiting(void);

int led_record_started(void);

int led_record_stopped(void);

int led_shutdown(void);

#ifdef __cplusplus
}
#endif