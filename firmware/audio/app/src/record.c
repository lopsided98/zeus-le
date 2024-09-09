#include "record.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>

#include "wav.h"

LOG_MODULE_REGISTER(record, LOG_LEVEL_DBG);

#define RECORD_FILE_DIR "/SD:"
#define RECORD_FILE_NAME_PREFIX "REC_"
#define RECORD_FILE_PATH_PREFIX RECORD_FILE_DIR "/" RECORD_FILE_NAME_PREFIX

// 2 GiB, because some programs use a signed 32-bit integer
// #define RECORD_MAX_FILE_BYTES UINT32_C((1 << 31) - 1)
// 512 MiB, because seek time increases with file size, and we need to regularly
// seek to the beginning to update the length in the header.
#define RECORD_MAX_FILE_BYTES UINT32_C(1 << 29)

#define RECORD_UPDATE_SIZE_INTERVAL_MS 10000

enum record_state {
    RECORD_STOPPED,
    RECORD_WAITING_START,
    RECORD_WAITING_NEW_FILE,
    RECORD_RUNNING,
};

K_MUTEX_DEFINE(record_mutex);

static struct record {
    struct k_mutex *mutex;

    bool init;
    /// Current open file
    struct fs_file_t file;
    /// Length of current file
    uint32_t file_len;
    /// Next unused file index
    uint32_t file_index;
    enum record_state state;
    uint32_t start_time;
    // Last time the WAV file size was updated (ms)
    int64_t last_size_update_time_ms;
} record = {
    .mutex = &record_mutex,
    .file_index = 0,
    .state = RECORD_STOPPED,
};

static int record_get_next_file_index(uint32_t *next_file_index) {
    int ret;
    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, RECORD_FILE_DIR);
    if (ret < 0) return ret;

    *next_file_index = 0;

    while (true) {
        struct fs_dirent entry;
        ret = fs_readdir(&dir, &entry);
        if (ret < 0) goto exit;

        size_t name_len = strlen(entry.name);
        if (name_len == 0) break;

        const size_t name_prefix_len = sizeof(RECORD_FILE_NAME_PREFIX) - 1;
        if (name_len < name_prefix_len) {
            // Too short
            continue;
        }
        if (memcmp(entry.name, RECORD_FILE_NAME_PREFIX, name_prefix_len) != 0) {
            // Prefix didn't match
            continue;
        }

        const char *index_start = entry.name + name_prefix_len;
        char *suffix_start;
        BUILD_ASSERT(sizeof(unsigned long) == sizeof(uint32_t),
                     "Cannot use strtoul() to parse uint32_t");
        uint32_t file_index =
            strtoul(entry.name + name_prefix_len, &suffix_start, 10);
        if (suffix_start == index_start) {
            // Did not parse any number
            continue;
        }
        if (file_index == ULONG_MAX) {
            // Index out of range, no available next index
            ret = -ERANGE;
            goto exit;
        }

        if (strcmp(suffix_start, ".wav") != 0) {
            // Suffix didn't match
            continue;
        }

        // +1 to record the next free index
        if ((file_index + 1) > *next_file_index) {
            *next_file_index = file_index + 1;
        }
    }

    ret = 0;

exit:
    fs_closedir(&dir);
    return ret;
}

int record_init(void) {
    struct record *r = &record;
    int ret;

    k_mutex_lock(r->mutex, K_FOREVER);
    if (r->init) {
        ret = -EALREADY;
        goto unlock;
    }

    fs_file_t_init(&r->file);

    ret = record_get_next_file_index(&r->file_index);
    if (ret < 0) {
        LOG_WRN("failed to get next file index (err %d)", ret);
        goto unlock;
    }

    r->init = true;
    ret = 0;

unlock:
    k_mutex_unlock(r->mutex);
    return ret;
}

int record_start(uint32_t time) {
    struct record *r = &record;
    int ret;

    k_mutex_lock(r->mutex, K_FOREVER);
    if (!r->init) {
        ret = -EINVAL;
        goto unlock;
    }

    switch (r->state) {
        case RECORD_STOPPED:
        case RECORD_WAITING_START:
            r->state = RECORD_WAITING_START;
            break;
        case RECORD_WAITING_NEW_FILE:
        case RECORD_RUNNING:
            r->state = RECORD_WAITING_NEW_FILE;
            break;
    }
    r->start_time = time;
    ret = 0;
    LOG_INF("start");

unlock:
    k_mutex_unlock(r->mutex);
    return ret;
}

int record_stop(void) {
    struct record *r = &record;
    int ret;

    k_mutex_lock(r->mutex, K_FOREVER);
    if (!r->init) {
        ret = -EINVAL;
        goto unlock;
    }

    switch (r->state) {
        case RECORD_STOPPED:
        case RECORD_WAITING_START:
            break;
        case RECORD_WAITING_NEW_FILE:
        case RECORD_RUNNING:
            err = wav_update_size(&r->file);
            if (err) {
                LOG_WRN("failed to update WAV length (err %d)", err);
            }
            fs_close(&r->file);
            break;
    }

    r->state = RECORD_STOPPED;
    ret = 0;

unlock:
    k_mutex_unlock(r->mutex);
    return ret;
}

int record_buffer(const struct audio_block *block) {
    struct record *r = &record;
    k_mutex_lock(r->mutex, K_FOREVER);

    int ret;
    if (!r->init) {
        ret = -EINVAL;
        goto unlock;
    }

    bool old_file = false;
    bool new_file;
    size_t split_offset;

    switch (r->state) {
        default:
        case RECORD_STOPPED:
            ret = 0;
            goto unlock;
        case RECORD_WAITING_NEW_FILE:
            old_file = true;
            // fallthrough
        case RECORD_WAITING_START: {
            uint32_t wait_time = r->start_time - block->start_time;
            LOG_INF("waiting: %" PRIu32, wait_time);
            if (wait_time <= block->duration) {
                new_file = true;
                // Byte index, but rounded to a frame boundary
                split_offset = DIV_ROUND_CLOSEST(wait_time * block->len /
                                                     block->bytes_per_frame,
                                                 block->duration) *
                               block->bytes_per_frame;
            } else {
                new_file = false;
                split_offset = block->len;
            }
        } break;
        case RECORD_RUNNING: {
            old_file = true;

            // Split file when it exceeds the max size
            uint32_t max_file_bytes =
                ROUND_DOWN(RECORD_MAX_FILE_BYTES, block->bytes_per_frame);

            if (r->file_len + block->len > max_file_bytes) {
                new_file = true;
                split_offset = max_file_bytes - r->file_len;
            } else {
                new_file = false;
                split_offset = block->len;
            }
        } break;
    }

    if (old_file) {
        LOG_INF("wav write");
        ret = wav_write(&r->file, block->buf, split_offset);
        if (ret) {
            LOG_ERR("WAV write failed (err %d)", ret);
            goto file_error;
        }
        r->file_len += split_offset;

        int64_t uptime_ms = k_uptime_get();
        if (uptime_ms - r->last_size_update_time_ms >=
            RECORD_UPDATE_SIZE_INTERVAL_MS) {
            LOG_INF("update size");
            ret = wav_update_size(&r->file);
            if (ret) {
                LOG_ERR("failed to update WAV size (err %d)", ret);
                goto file_error;
            }

            LOG_INF("sync");
            ret = fs_sync(&r->file);
            if (ret) {
                LOG_ERR("failed to sync WAV file (err %d)", ret);
                goto file_error;
            }

            r->last_size_update_time_ms = uptime_ms;
        }
    }
    if (new_file) {
        if (old_file) {
            ret = wav_update_size(&r->file);
            if (ret) {
                LOG_WRN("failed to update WAV size (err %d)", ret);
            }
            // Ignore error, what am I going to do?
            fs_close(&r->file);
        }
        r->file_len = 0;
        r->last_size_update_time_ms = k_uptime_get();

        char file_name[sizeof(RECORD_FILE_PATH_PREFIX) /* includes terminator */
                       + 10 /* max digits */ + 4 /*.wav*/];
        ret = snprintf(file_name, sizeof(file_name),
                       RECORD_FILE_PATH_PREFIX "%04" PRIu32 ".wav",
                       r->file_index);
        if (ret < 0) {
            goto error;
        } else if (ret >= sizeof(file_name)) {
            ret = -EOVERFLOW;
            goto error;
        }

        ret = fs_open(&r->file, file_name, FS_O_WRITE | FS_O_CREATE);
        if (ret) {
            LOG_ERR("failed to create file: %s (err %d)", file_name, ret);
            goto file_error;
        }
        r->file_index++;

        // TODO: don't hardcode format
        ret = wav_init(&r->file, &(struct wav_format){
                                     .channels = 2,
                                     .sample_rate = 44100,
                                     .bits_per_sample = 16,
                                 });
        if (ret) {
            LOG_ERR("WAV init failed (err %d)", ret);
            goto file_error;
        }

        size_t write_len = block->len - split_offset;
        ret = wav_write(&r->file, block->buf + split_offset, write_len);
        if (ret) {
            LOG_ERR("WAV write failed (err %d)", ret);
            goto file_error;
        }
        r->file_len += write_len;

        r->state = RECORD_RUNNING;
    }

    ret = 0;
    goto unlock;

file_error:
    fs_close(&r->file);

error:
    r->state = RECORD_STOPPED;

unlock:
    k_mutex_unlock(r->mutex);
    return ret;
}