#pragma once

#include <stdint.h>

struct zeus_packet_timer_msg {
    uint8_t seq;
    uint32_t timer;
};