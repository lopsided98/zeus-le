#pragma once

#include <stdint.h>
#include <zephyr/fs/fs.h>

struct wav_format {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t max_file_size;
};

struct wav {
    struct fs_file_t fp;
    uint16_t bytes_per_frame;
    uint32_t max_data_size;
    uint32_t data_size;
};

/// Open a new WAV file for writing. File will be truncated if it already
/// exists.
int wav_open(struct wav* w, const char* name, const struct wav_format* fmt);

/// Write data to a WAV file. Must have been initialized with wav_init(). The
/// file length in the header is not updated, and wav_update_size() must be
/// called periodically to keep it up to date.
int wav_write(struct wav* w, const uint8_t* buf, size_t len);

/// Update the file size fields in the WAV header.
int wav_update_size(struct wav* w);

/// Update the file size and then close the file. The file is still closed even
/// if the size update fails.
int wav_close(struct wav* w);

/// Close file updating header.
int wav_close_no_update(struct wav* w);
