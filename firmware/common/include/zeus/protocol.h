#pragma once

#include <stdint.h>

struct zeus_adv_data {
    uint8_t seq;
    uint32_t timer;
} __attribute__((packed));