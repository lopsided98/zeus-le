// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <nrfx_timer.h>
#include <stdint.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys/onoff.h>

#include "freq_est.h"
#include "zeus/protocol.h"

#define SYNC_TIMER_FREQ 16000000

int sync_timer_init(void);

void sync_timer_recv_adv(const struct zeus_adv_data *data);

uint8_t sync_timer_get_i2s_dppi(void);

uint32_t sync_timer_get_i2s_time(void);

void sync_timer_correct_time(qu32_32 *time);