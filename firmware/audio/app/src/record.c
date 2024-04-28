#include "record.h"

#include <stdint.h>
#include <stdio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>

#include "wav.h"

LOG_MODULE_REGISTER(record, LOG_LEVEL_DBG);

#define RECORD_FILE_DIR "/SD:"
#define RECORD_FILE_NAME_PREFIX "REC_"
#define RECORD_FILE_PATH_PREFIX RECORD_FILE_DIR "/" RECORD_FILE_NAME_PREFIX

enum record_state {
    RECORD_STOPPED,
    RECORD_WAITING_START,
    RECORD_WAITING_NEW_FILE,
    RECORD_RUNNING,
};

K_MUTEX_DEFINE(record_mutex);

static struct record {
    struct k_mutex *mutex;

    /// Current open file
    struct fs_file_t file;
    /// Next unused file index
    uint32_t file_index;
    enum record_state state;
    uint32_t start_time;
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

        if (entry.name[0] == 0) break;

        uint32_t file_index;
        size_t scan_len;
        ret = sscanf(entry.name, RECORD_FILE_NAME_PREFIX "%" SCNu32 ".wav%zn",
                     &file_index, &scan_len);
        if (ret < 0) return ret;

        // Didn't match file index
        if (ret != 1) continue;
        // Didn't match entire file name
        if (scan_len != strlen(entry.name)) continue;

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

    fs_file_t_init(&r->file);

    ret = record_get_next_file_index(&r->file_index);
    if (ret < 0) {
        LOG_WRN("failed to get next file index (err %d)", ret);
    }

    return 0;
}

void record_start(uint32_t time) {
    struct record *r = &record;
    k_mutex_lock(r->mutex, K_FOREVER);

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
    LOG_INF("start");

    k_mutex_unlock(r->mutex);
}

void record_stop(void) {
    struct record *r = &record;
    k_mutex_lock(r->mutex, K_FOREVER);

    switch (r->state) {
        case RECORD_STOPPED:
        case RECORD_WAITING_START:
            break;
        case RECORD_WAITING_NEW_FILE:
        case RECORD_RUNNING:
            fs_close(&r->file);
            break;
    }

    r->state = RECORD_STOPPED;

    k_mutex_unlock(r->mutex);
}

int record_buffer(const struct audio_block *block) {
    struct record *r = &record;
    int ret;

    k_mutex_lock(r->mutex, K_FOREVER);
    bool old_file = false;
    bool new_file;
    size_t split_offset;

    // LOG_INF("state: %d", r->state);
    switch (r->state) {
        case RECORD_STOPPED:
            goto unlock;
        case RECORD_WAITING_NEW_FILE:
            old_file = true;
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
        case RECORD_RUNNING:
            old_file = true;
            new_file = false;
            split_offset = block->len;
            break;
    }

    if (old_file) {
        ret = wav_write(&r->file, block->buf, split_offset);
        if (ret) {
            LOG_ERR("WAV write failed (err %d)", ret);
            goto file_error;
        }
    }
    if (new_file) {
        if (old_file) {
            // Ignore error, what am I going to do?
            fs_close(&r->file);
        }
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

        ret = wav_write(&r->file, block->buf + split_offset,
                        block->len - split_offset);
        if (ret) {
            LOG_ERR("WAV write failed (err %d)", ret);
            goto file_error;
        }

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