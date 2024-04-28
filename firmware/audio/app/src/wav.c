#include "wav.h"

#include <errno.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define WAV_CHUNK_SIZE_OFFSET (4)
#define WAV_SUBCHUNK_2_SIZE_OFFSET (40)
#define WAV_HEADER_SIZE (44)

static int wav_write_buf(struct fs_file_t* fp, const void* buf, size_t len) {
    int ret = fs_write(fp, buf, len);
    if (ret < 0) {
        return ret;
    } else if (ret != len) {
        return -errno;
    } else {
        return 0;
    }
}

static int wav_write_u16(struct fs_file_t* fp, uint16_t val) {
    uint8_t buf[sizeof(val)];
    sys_put_le16(val, buf);
    return wav_write_buf(fp, buf, sizeof(buf));
}

static int wav_write_u32(struct fs_file_t* fp, uint32_t val) {
    uint8_t buf[sizeof(val)];
    sys_put_le32(val, buf);
    return wav_write_buf(fp, buf, sizeof(buf));
}

static int wav_update_size(struct fs_file_t* fp) {
    int ret = fs_seek(fp, 0, FS_SEEK_END);
    if (ret < 0) return ret;

    off_t size = fs_tell(fp);
    if (size < 0) return size;

    if (size < WAV_HEADER_SIZE) {
        return -EINVAL;
    } else if (size > UINT32_MAX) {
        return -EFBIG;
    }

    ret = fs_seek(fp, WAV_CHUNK_SIZE_OFFSET, FS_SEEK_SET);
    if (ret < 0) return ret;
    ret = wav_write_u32(fp, size - 8);
    if (ret < 0) return ret;

    ret = fs_seek(fp, WAV_SUBCHUNK_2_SIZE_OFFSET, FS_SEEK_SET);
    if (ret < 0) return ret;
    ret = wav_write_u32(fp, size - WAV_HEADER_SIZE);
    if (ret < 0) return ret;

    ret = fs_seek(fp, 0, FS_SEEK_END);
    if (ret < 0) return ret;

    return 0;
}

int wav_init(struct fs_file_t* fp, const struct wav_format* fmt) {
    uint16_t bytes_per_sample = DIV_ROUND_UP(fmt->bits_per_sample, 8);
    uint32_t byte_rate = fmt->sample_rate * fmt->channels * bytes_per_sample;
    uint16_t block_align = fmt->channels * bytes_per_sample;

    int ret = fs_truncate(fp, 0);
    if (ret < 0) return ret;

    // Chunk ID
    ret = wav_write_buf(fp, "RIFF", 4);
    if (ret < 0) return ret;
    // Chunk Size, initially 36 bytes with no data
    ret = wav_write_u32(fp, 36);
    if (ret < 0) return ret;
    // Format
    ret = wav_write_buf(fp, "WAVE", 4);
    if (ret < 0) return ret;

    // Subchunk 1 ID
    ret = wav_write_buf(fp, "fmt ", 4);
    if (ret < 0) return ret;
    // Subchunk 1 Size
    ret = wav_write_u32(fp, 16);
    if (ret < 0) return ret;
    // Audio Format
    ret = wav_write_u16(fp, 1 /* PCM */);
    if (ret < 0) return ret;
    // Num Channels
    ret = wav_write_u16(fp, fmt->channels);
    if (ret < 0) return ret;
    // Sample Rate
    ret = wav_write_u32(fp, fmt->sample_rate);
    if (ret < 0) return ret;
    // Byte Rate
    ret = wav_write_u32(fp, byte_rate);
    if (ret < 0) return ret;
    // Block Align
    ret = wav_write_u16(fp, block_align);
    if (ret < 0) return ret;
    // Bits per Sample
    ret = wav_write_u16(fp, fmt->bits_per_sample);
    if (ret < 0) return ret;

    // Subchunk 2 ID
    ret = wav_write_buf(fp, "data", 4);
    if (ret < 0) return ret;
    // Subchunk 2 Size, initially zero
    ret = wav_write_u32(fp, 0);
    if (ret < 0) return ret;

    return 0;
}

int wav_write(struct fs_file_t* fp, const uint8_t* buf, size_t len) {
    int ret = fs_write(fp, buf, len);
    if (ret < 0) {
        return ret;
    } else if (ret != len) {
        // Zephyr never does partial writes except on failure, normally out
        // of space.
        int write_errno = errno;
        // Sometimes errno is zero, unclear why. Assume no space.
        if (write_errno == 0) {
            write_errno = ENOSPC;
        }

        // Update the size anyway
        ret = wav_update_size(fp);
        if (ret < 0) return ret;

        return -write_errno;
    }

    return wav_update_size(fp);
}