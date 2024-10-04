#pragma once

#include <stdint.h>

#include "audio.h"

#ifdef __cplusplus
extern "C" {
#endif

int record_init(void);

int record_start(uint32_t time);

int record_buffer(const struct audio_block *block);

int record_stop(void);

/// Stop any in-progress recording and prevent new recordings from startings.
int record_shutdown(void);

#ifdef __cplusplus
}
#endif