// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

#include "freq_est.h"
#include "zeus/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

int sync_timer_init(void);

void sync_timer_recv_adv(const struct zeus_adv_header *hdr);

uint8_t sync_timer_get_i2s_dppi(void);

uint8_t sync_timer_get_usb_sof_dppi(void);

uint32_t sync_timer_get_i2s_time(void);

uint32_t sync_timer_get_usb_sof_time(void);

/// Get an estimate of the current central time. Should not be used for precise
/// timing.
qu32_32 sync_timer_get_central_time(void);

/// Adjust a local sync timer measurement to the corresponding central time
/// measurement. Returns true if the time was adjusted successfully.
bool sync_timer_correct_time(qu32_32 *time);

#ifdef __cplusplus
}
#endif