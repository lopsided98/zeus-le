#include "wav.h"

#include <errno.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define WAV_CHUNK_SIZE_OFFSET (4)
#define WAV_SUBCHUNK_2_SIZE_OFFSET (40)
#define WAV_HEADER_SIZE (44)

static int wav_write_buf(struct wav* w, const void* buf, size_t len) {
    int ret = fs_write(&w->fp, buf, len);
    if (ret < 0) {
        return ret;
    } else if (ret != len) {
        return -errno;
    } else {
        return 0;
    }
}

static int wav_write_u16(struct wav* w, uint16_t val) {
    uint8_t buf[sizeof(val)];
    sys_put_le16(val, buf);
    return wav_write_buf(w, buf, sizeof(buf));
}

static int wav_write_u32(struct wav* w, uint32_t val) {
    uint8_t buf[sizeof(val)];
    sys_put_le32(val, buf);
    return wav_write_buf(w, buf, sizeof(buf));
}

static int wav_update_size(struct wav* w) {
    int ret = fs_seek(&w->fp, 0, FS_SEEK_END);
    if (ret < 0) return ret;

    off_t size = fs_tell(&w->fp);
    if (size < 0) return size;

    if (size < WAV_HEADER_SIZE) {
        return -EINVAL;
    } else if (size > UINT32_MAX) {
        return -EFBIG;
    }

    ret = fs_seek(&w->fp, WAV_CHUNK_SIZE_OFFSET, FS_SEEK_SET);
    if (ret < 0) return ret;
    ret = wav_write_u32(w, size - 8);
    if (ret < 0) return ret;

    ret = fs_seek(&w->fp, WAV_SUBCHUNK_2_SIZE_OFFSET, FS_SEEK_SET);
    if (ret < 0) return ret;
    ret = wav_write_u32(w, size - WAV_HEADER_SIZE);
    if (ret < 0) return ret;

    ret = fs_seek(&w->fp, 0, FS_SEEK_END);
    if (ret < 0) return ret;

    return 0;
}

int wav_create(struct wav* w, const char* path, const struct wav_format* fmt) {
    uint16_t bytes_per_sample = DIV_ROUND_UP(fmt->bits_per_sample, 8);
    uint32_t byte_rate = fmt->sample_rate * fmt->channels * bytes_per_sample;
    uint16_t block_align = fmt->channels * bytes_per_sample;

    int ret = fs_open(&w->fp, path, FS_O_WRITE | FS_O_CREATE);
    if (ret < 0) return ret;

    // Chunk ID
    ret = wav_write_buf(w, "RIFF", 4);
    if (ret < 0) goto error;
    // Chunk Size, initially 36 bytes with no data
    ret = wav_write_u32(w, 36);
    if (ret < 0) goto error;
    // Format
    ret = wav_write_buf(w, "WAVE", 4);
    if (ret < 0) goto error;

    // Subchunk 1 ID
    ret = wav_write_buf(w, "fmt ", 4);
    if (ret < 0) goto error;
    // Subchunk 1 Size
    ret = wav_write_u32(w, 16);
    if (ret < 0) goto error;
    // Audio Format
    ret = wav_write_u16(w, 1 /* PCM */);
    if (ret < 0) goto error;
    // Num Channels
    ret = wav_write_u16(w, fmt->channels);
    if (ret < 0) goto error;
    // Sample Rate
    ret = wav_write_u32(w, fmt->sample_rate);
    if (ret < 0) goto error;
    // Byte Rate
    ret = wav_write_u32(w, byte_rate);
    if (ret < 0) goto error;
    // Block Align
    ret = wav_write_u16(w, block_align);
    if (ret < 0) goto error;
    // Bits per Sample
    ret = wav_write_u16(w, fmt->bits_per_sample);
    if (ret < 0) goto error;

    // Subchunk 2 ID
    ret = wav_write_buf(w, "data", 4);
    if (ret < 0) goto error;
    // Subchunk 2 Size, initially zero
    ret = wav_write_u32(w, 0);
    if (ret < 0) goto error;

    return 0;

error:
    fs_close(&w->fp);
    return ret;
}

int wav_write(struct wav* w, const uint8_t* buf, size_t len) {
    int ret = fs_write(&w->fp, buf, len);
    if (ret < 0) return ret;

    return wav_update_size(w);
}

int wav_close(struct wav* w) { return fs_close(&w->fp); }