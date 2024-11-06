#include "wav.h"

#include <errno.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define WAV_HEADER_SIZE (44)
#define WAV_CHUNK_SIZE_OFFSET (4)
#define WAV_SUBCHUNK_2_SIZE_OFFSET (40)

static int wav_write_all(struct fs_file_t* fp, const void* buf, size_t len) {
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
        return -write_errno;
    } else {
        return 0;
    }
}

static int wav_write_u16(struct fs_file_t* fp, uint16_t val) {
    uint8_t buf[sizeof(val)];
    sys_put_le16(val, buf);
    return wav_write_all(fp, buf, sizeof(buf));
}

static int wav_write_u32(struct fs_file_t* fp, uint32_t val) {
    uint8_t buf[sizeof(val)];
    sys_put_le32(val, buf);
    return wav_write_all(fp, buf, sizeof(buf));
}

static int wav_write_header(struct wav* w, const struct wav_format* fmt) {
    int ret;

    uint16_t bytes_per_sample = DIV_ROUND_UP(fmt->bits_per_sample, 8);
    uint16_t bytes_per_frame = fmt->channels * bytes_per_sample;
    uint32_t byte_rate = fmt->sample_rate * bytes_per_frame;

    w->bytes_per_frame = bytes_per_frame;
    // Limit of data chunk size; make sure it doesn't split a frame
    w->max_data_size =
        ROUND_DOWN(fmt->max_file_size - WAV_HEADER_SIZE, bytes_per_frame);
    w->data_size = 0;

    // Chunk ID
    ret = wav_write_all(&w->fp, "RIFF", 4);
    if (ret < 0) return ret;
    // Chunk Size, initially set to maximum allowed file size. The size is
    // updated to the correct value when the file is closed, but doing it
    // regularly as the file is written is too slow because seeking on FatFs
    // becomes slower as the file gets longer. Setting the maximum length should
    // at least allow the file to be played even if it doesn't get closed
    // cleanly.
    ret = wav_write_u32(&w->fp, w->max_data_size + WAV_HEADER_SIZE - 8);
    if (ret < 0) return ret;
    // Format
    ret = wav_write_all(&w->fp, "WAVE", 4);
    if (ret < 0) return ret;

    // Subchunk 1 ID
    ret = wav_write_all(&w->fp, "fmt ", 4);
    if (ret < 0) return ret;
    // Subchunk 1 Size
    ret = wav_write_u32(&w->fp, 16);
    if (ret < 0) return ret;
    // Audio Format
    ret = wav_write_u16(&w->fp, 1 /* PCM */);
    if (ret < 0) return ret;
    // Num Channels
    ret = wav_write_u16(&w->fp, fmt->channels);
    if (ret < 0) return ret;
    // Sample Rate
    ret = wav_write_u32(&w->fp, fmt->sample_rate);
    if (ret < 0) return ret;
    // Byte Rate
    ret = wav_write_u32(&w->fp, byte_rate);
    if (ret < 0) return ret;
    // Block Align
    ret = wav_write_u16(&w->fp, bytes_per_frame);
    if (ret < 0) return ret;
    // Bits per Sample
    ret = wav_write_u16(&w->fp, fmt->bits_per_sample);
    if (ret < 0) return ret;

    // Subchunk 2 ID
    ret = wav_write_all(&w->fp, "data", 4);
    if (ret < 0) return ret;
    // Subchunk 2 Size, initially set the maximum allowed size. See the comment
    // about chunk size above.
    ret = wav_write_u32(&w->fp, w->max_data_size);
    if (ret < 0) return ret;

    return 0;
}

int wav_open(struct wav* w, const char* name, const struct wav_format* fmt) {
    if (fmt->channels == 0) return -EINVAL;
    if (fmt->sample_rate == 0) return -EINVAL;
    if (fmt->bits_per_sample == 0) return -EINVAL;
    if (fmt->max_file_size < WAV_HEADER_SIZE) return -EINVAL;

    *w = (struct wav){.data_size = 0};
    fs_file_t_init(&w->fp);
    int ret = fs_open(&w->fp, name, FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    if (ret < 0) return ret;

    ret = wav_write_header(w, fmt);
    if (ret < 0) {
        fs_close(&w->fp);
        return ret;
    }

    return 0;
}

int wav_write(struct wav* w, const uint8_t buf[], uint32_t len) {
    if (w->data_size + len > w->max_data_size) {
        len = w->max_data_size - w->data_size;
    }
    int ret = fs_write(&w->fp, buf, len);
    if (ret < 0) {
        return ret;
    } else if (ret != len) {
        // Zephyr never does partial writes except on failure, normally out
        // of space.
        w->data_size += ret;
        int write_errno = errno;
        // Sometimes errno is zero, unclear why. Assume no space.
        if (write_errno == 0) {
            write_errno = ENOSPC;
        }
        return -write_errno;
    } else {
        w->data_size += len;
        return len;
    }
}

int wav_update_size(struct wav* w) {
    // If we ran out of space or the user provided a partial frame in a buffer,
    // we could have a data size that is not a multiple of the frame size. Round
    // it down when writing the header, but don't truncate the file in case the
    // user provides the rest of the frame later.
    uint32_t data_size = ROUND_DOWN(w->data_size, w->bytes_per_frame);

    int ret = fs_seek(&w->fp, WAV_CHUNK_SIZE_OFFSET, FS_SEEK_SET);
    if (ret < 0) return ret;
    ret = wav_write_u32(&w->fp, data_size + WAV_HEADER_SIZE - 8);
    if (ret < 0) return ret;

    ret = fs_seek(&w->fp, WAV_SUBCHUNK_2_SIZE_OFFSET, FS_SEEK_SET);
    if (ret < 0) return ret;
    ret = wav_write_u32(&w->fp, data_size);
    if (ret < 0) return ret;

    ret = fs_seek(&w->fp, 0, FS_SEEK_END);
    if (ret < 0) return ret;

    return 0;
}

int wav_close(struct wav* w) {
    int ret = wav_update_size(w);
    int ret_close = fs_close(&w->fp);
    if (ret == 0) ret = ret_close;
    return ret;
}

int wav_close_no_update(struct wav* w) { return fs_close(&w->fp); }
