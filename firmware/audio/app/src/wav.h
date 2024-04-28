#pragma once

#include <stdint.h>
#include <zephyr/fs/fs.h>

struct wav_format {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
};

int wav_init(struct fs_file_t* fp, const struct wav_format* fmt);

int wav_write(struct fs_file_t* fp, const uint8_t* buf, size_t len);