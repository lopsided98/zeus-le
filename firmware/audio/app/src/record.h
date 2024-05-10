#pragma once

#include <stdint.h>

#include "audio.h"

#ifdef __cplusplus
extern "C" {
#endif

int record_init(void);

int record_start(uint32_t time);

int record_stop(void);

int record_buffer(const struct audio_block *block);

#ifdef __cplusplus
}
#endif