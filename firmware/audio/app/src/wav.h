#pragma once

#include <stdint.h>
#include <zephyr/fs/fs.h>

struct wav_format {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
};

struct wav {
    struct fs_file_t fp;
};