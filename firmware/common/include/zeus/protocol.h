// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

struct zeus_adv_data {
    uint8_t seq;
    uint32_t time;
} __attribute__((packed));