// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct audio_block {
    uint8_t* buf;
    size_t len;
    uint32_t start_time;
    uint32_t duration;
    uint8_t bytes_per_frame;
};

int audio_init(void);

#ifdef __cplusplus
}
#endif