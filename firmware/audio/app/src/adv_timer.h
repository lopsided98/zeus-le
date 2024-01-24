// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <nrfx_timer.h>
#include <stdint.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys/onoff.h>

#include "zeus/protocol.h"

#define ADV_TIMER_FREQ 16000000

struct adv_timer {
    // Resources
    struct mbox_channel channel;
    nrfx_timer_t timer;
    struct onoff_client hf_cli;

    // State
    /// True if a previous advertisment has been received
    bool last_time_valid;
    /// Sequence number of the advertisment whose time is captured in last_time.
    uint8_t last_time_seq;
    /// Local timestamp of the last received advertisment
    uint32_t last_time;
};

int adv_timer_init(struct adv_timer *t);

bool adv_timer_recv(struct adv_timer *t, const struct zeus_adv_data *data,
                    uint32_t *local_time, uint32_t *central_time);