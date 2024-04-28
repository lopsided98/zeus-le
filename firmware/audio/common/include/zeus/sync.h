// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

#define ZEUS_PACKET_END_MBOX_CHANNEL 4

struct zeus_sync_msg {
    uint8_t seq;
    uint32_t time;
};