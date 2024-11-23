#pragma once

#include <stdint.h>

#include "audio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_FILE_NAME_PREFIX_LEN 32

int record_init(void);

int record_get_file_name_prefix(char *prefix, size_t len);

int record_set_file_name_prefix(const char *prefix);

int record_card_inserted(void);

int record_start(uint32_t time);

int record_buffer(const struct audio_block *block);

int record_stop(void);

/// Stop any in-progress recording and prevent new recordings from startings.
int record_shutdown(void);

#ifdef __cplusplus
}
#endif