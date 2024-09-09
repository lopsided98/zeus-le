#pragma once

#include <stdint.h>
#include <zephyr/fs/fs.h>

// 2 GiB, because some programs use a signed 32-bit integer
#define WAV_MAX_SIZE UINT32_C((1 << 31) - 1)

struct wav_format {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
};

/// Initialize an empty WAV file. File will be truncated if not empty.
int wav_init(struct fs_file_t* fp, const struct wav_format* fmt);

/// Write data to a WAV file. Must have been initialized with wav_init(). The
/// file length in the header is not updated, and wav_update_size() must be
/// called periodically to keep it up to date.
int wav_write(struct fs_file_t* fp, const uint8_t* buf, size_t len);

/// Update the file length fields in the WAV header.
int wav_update_size(struct fs_file_t* fp);