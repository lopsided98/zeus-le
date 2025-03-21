#include "record.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "wav.h"
#include "zeus/led.h"
#include "zeus/util.h"

LOG_MODULE_REGISTER(record, LOG_LEVEL_DBG);

#define RECORD_FILE_DIR "/SD:"
// 2 GiB, because some programs use a signed 32-bit integer
#define RECORD_FILE_MAX_SIZE INT32_MAX

#define RECORD_SYNC_INTERVAL_MS 5000

enum record_state {
    RECORD_STOPPED,
    RECORD_WAITING_START,
    RECORD_WAITING_NEW_FILE,
    RECORD_RUNNING,
};

K_MUTEX_DEFINE(record_mutex);

K_THREAD_STACK_DEFINE(record_close_thread_stack, 1024);
K_MSGQ_DEFINE(record_close_queue, sizeof(struct wav), 1, 1);

static const struct record_config {
    struct k_mutex *mutex;
    struct k_msgq *close_queue;
} record_config = {
    .mutex = &record_mutex,
    .close_queue = &record_close_queue,
};

static struct record_data {
    struct k_thread close_thread;

    bool init;
    char file_name_prefix[RECORD_FILE_NAME_PREFIX_LEN];
    /// Current open file
    struct wav file;
    /// Next unused file index
    uint32_t file_index;
    enum record_state state;
    uint32_t start_time;
    // Last time the WAV file size was updated (ms)
    int64_t last_sync_time_ms;
} record_data = {
    .file_name_prefix = "REC",
    .file_index = 0,
    .state = RECORD_STOPPED,
};

static void record_close_thread_run(void *p1, void *p2, void *p3) {
    const struct record_config *config = &record_config;
    int err;

    while (true) {
        struct wav file;
        err = k_msgq_get(config->close_queue, &file, K_FOREVER);
        if (err < 0) {
            LOG_WRN("failed to get queue item  (err %d)", err);
            continue;
        }

        err = wav_close(&file);
        if (err < 0) {
            LOG_WRN("failed to close file (err %d)", err);
        }
    }
}

static int record_find_next_file_index(void) {
    struct record_data *data = &record_data;
    struct fs_dir_t dir;
    int ret;
    fs_dir_t_init(&dir);
    ret = fs_opendir(&dir, RECORD_FILE_DIR);
    if (ret) return ret;

    data->file_index = 0;

    while (true) {
        struct fs_dirent entry;
        ret = fs_readdir(&dir, &entry);
        if (ret < 0) goto exit;

        size_t name_len = strlen(entry.name);
        if (name_len == 0) break;

        const size_t name_prefix_len = strlen(data->file_name_prefix);
        // Prefix + underscore
        if (name_len < name_prefix_len + 1) {
            // Too short
            continue;
        }
        if (memcmp(entry.name, data->file_name_prefix, name_prefix_len) != 0) {
            // Prefix didn't match
            continue;
        }
        if (entry.name[name_prefix_len] != '_') {
            // No underscore after prefix
            continue;
        }

        const char *index_start =
            entry.name + name_prefix_len + 1 /* underscore */;
        char *suffix_start;
        BUILD_ASSERT(sizeof(unsigned long) == sizeof(uint32_t),
                     "Cannot use strtoul() to parse uint32_t");
        uint32_t file_index = strtoul(index_start, &suffix_start, 10);
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
        if ((file_index + 1) > data->file_index) {
            data->file_index = file_index + 1;
        }
    }

    ret = 0;

exit:
    fs_closedir(&dir);
    return ret;
}

static void record_close_file(void) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;
    int err = k_msgq_put(config->close_queue, &data->file, K_FOREVER);
    if (err < 0) {
        LOG_WRN("Could not close file in background (err %d)", err);
        // Just close synchronously without updating size
        wav_close_no_update(&data->file);
    }
}

/// Callback for settings_load_subtree_direct() to apply recording settings.
static int record_settings_load_cb(const char *key, size_t len,
                                   settings_read_cb read_cb, void *cb_arg,
                                   void *param) {
    struct record_data *data = &record_data;
    int ret;

    if (0 == strcmp(key, "prefix")) {
        // Temporary buffer to avoid overwriting old value on failure
        char file_name_prefix[sizeof(data->file_name_prefix)];
        ret = read_cb(cb_arg, file_name_prefix, sizeof(file_name_prefix) - 1);
        if (ret == 0) {
            // Deleted
            return 0;
        } else if (ret < 0) {
            LOG_WRN("failed to read setting: %s (read %d)", key, ret);
            return 0;
        }
        // Ensure null-termination
        file_name_prefix[ret] = '\0';
        memcpy(data->file_name_prefix, file_name_prefix,
               sizeof(file_name_prefix));
    } else {
        LOG_WRN("unknown record setting: %s", key);
        return 0;
    }

    return 0;
}

int record_init(void) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;
    int ret;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (data->init) return -EALREADY;

    ret = settings_load_subtree_direct("rec", record_settings_load_cb, NULL);
    if (ret) {
        LOG_WRN("failed to load settings (err %d)", ret);
    }

    k_thread_create(&data->close_thread, record_close_thread_stack,
                    K_THREAD_STACK_SIZEOF(record_close_thread_stack),
                    record_close_thread_run, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
    k_thread_name_set(&data->close_thread, "record_close");

    ret = record_find_next_file_index();
    if (ret < 0) {
        // Continue anyway, probably means no SD card
        LOG_WRN("failed to find next file index (err %d)", ret);
    }

    data->init = true;
    return 0;
}

int record_get_file_name_prefix(char *prefix, size_t len) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (!data->init) return -EINVAL;

    size_t prefix_len = strlen(data->file_name_prefix);
    if (prefix_len >= len) {
        return -EINVAL;
    }
    memcpy(prefix, data->file_name_prefix, prefix_len + 1);

    return 0;
}

int record_set_file_name_prefix(const char *prefix) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;
    int ret;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (!data->init) return -EINVAL;

    size_t len = strlen(prefix);
    if (len >= sizeof(data->file_name_prefix)) {
        return -EINVAL;
    }
    memcpy(data->file_name_prefix, prefix, len + 1);

    ret = settings_save_one("rec/prefix", prefix, len);
    if (ret) return ret;

    ret = record_find_next_file_index();
    if (ret) {
        // Okay, SD card might not be inserted
        LOG_WRN("failed to find next file index (err %d)", ret);
    }

    return 0;
}

int record_card_inserted(void) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;
    int ret;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (!data->init) return -EINVAL;

    ret = record_find_next_file_index();
    if (ret < 0) {
        LOG_WRN("failed to find next file index (err %d)", ret);
        return ret;
    }
    return 0;
}

int record_start(uint32_t time) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;
    int ret;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (!data->init) return -EINVAL;

    ret = audio_start();
    if (ret && ret != -EALREADY) {
        return ret;
    }

    switch (data->state) {
        case RECORD_STOPPED:
        case RECORD_WAITING_START:
            data->state = RECORD_WAITING_START;
            break;
        case RECORD_WAITING_NEW_FILE:
        case RECORD_RUNNING:
            data->state = RECORD_WAITING_NEW_FILE;
            break;
    }
    data->start_time = time;

    led_record_waiting();
    LOG_INF("start");

    return 0;
}

int record_buffer(const struct audio_block *block) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;
    int ret;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (!data->init) return -EINVAL;

    __ASSERT(block->len % block->bytes_per_frame == 0,
             "Block contains partial frame");

    bool old_file = false;
    bool new_file;
    size_t split_offset;

    switch (data->state) {
        default:
        case RECORD_STOPPED:
            return 0;
        case RECORD_WAITING_NEW_FILE:
            old_file = true;
            // fallthrough
        case RECORD_WAITING_START: {
            int32_t wait_time = (int32_t)(data->start_time - block->start_time);
            // If start time is in the past (according to serial number
            // arithmetic), start immediately.
            if (wait_time < 0) {
                LOG_WRN("missed start time by %" PRIu32, -wait_time);
                wait_time = 0;
            } else {
                LOG_INF("waiting: %" PRIu32, wait_time);
            }
            if (wait_time <= block->duration) {
                new_file = true;
                uint32_t block_frames = block->len / block->bytes_per_frame;
                uint32_t split_frame = DIV_ROUND_CLOSEST(
                    wait_time * block_frames, block->duration);
                // Byte index, rounded to a frame boundary
                split_offset = split_frame * block->bytes_per_frame;
                led_record_started();
            } else {
                new_file = false;
                split_offset = block->len;
            }
        } break;
        case RECORD_RUNNING: {
            old_file = true;
            new_file = false;
            split_offset = block->len;
        } break;
    }

    led_record_sync(block->start_time);

    if (old_file) {
        ret = wav_write(&data->file, block->buf, split_offset);
        if (ret < 0) {
            LOG_ERR("WAV write failed (err %d)", ret);
            goto file_error;
        } else if (ret != split_offset) {
            // File exceeded max size, split into a new file
            // FIXME: there is an extremely rare edge case here where a start
            // command could be triggered during the same buffer, and we would
            // need to split the buffer across three files to provide completely
            // correct behavior.
            new_file = true;
            split_offset = ret;
        }

        int64_t uptime_ms = k_uptime_get();
        if (uptime_ms - data->last_sync_time_ms >= RECORD_SYNC_INTERVAL_MS) {
            // LOG_INF("sync");
            ret = fs_sync(&data->file.fp);
            if (ret) {
                LOG_ERR("WAV file sync failed (err %d)", ret);
                goto file_error;
            }

            data->last_sync_time_ms = uptime_ms;
        }
    }
    if (new_file) {
        LOG_INF("new file, len: %u, split: %u", block->len, split_offset);
        if (old_file) {
            record_close_file();
        }
        data->last_sync_time_ms = k_uptime_get();

        char file_name[(sizeof(RECORD_FILE_DIR) - 1) + 1 /* / */ +
                       (sizeof(data->file_name_prefix) - 1) + 1 /* _ */ +
                       10 /* max digits */
                       + 4 /* .wav */ + 1 /* terminator */];
        ret = snprintf(file_name, sizeof(file_name),
                       RECORD_FILE_DIR "/%s_%04" PRIu32 ".wav",
                       data->file_name_prefix, data->file_index);
        if (ret < 0) {
            goto error;
        } else if (ret >= sizeof(file_name)) {
            ret = -EOVERFLOW;
            goto error;
        }

        LOG_INF("creating new file: %s", file_name);

        // TODO: don't hardcode format
        ret = wav_open(&data->file, file_name,
                       &(struct wav_format){
                           .channels = 2,
                           .sample_rate = 48000,
                           .bits_per_sample = 16,
                           .max_file_size = RECORD_FILE_MAX_SIZE,
                       });
        if (ret) {
            LOG_ERR("failed to create file: %s (err %d)", file_name, ret);
            goto file_error;
        }
        data->file_index++;

        size_t write_len = block->len - split_offset;
        ret = wav_write(&data->file, block->buf + split_offset, write_len);
        if (ret != write_len) {
            LOG_ERR("WAV write failed (err %d)", ret);
            goto file_error;
        }

        data->state = RECORD_RUNNING;
    }

    return 0;

file_error:
    record_close_file();

error:
    data->state = RECORD_STOPPED;

    return ret;
}

static int record_stop_unlocked(void) {
    struct record_data *data = &record_data;

    if (!data->init) {
        return -EINVAL;
    }

    switch (data->state) {
        case RECORD_STOPPED:
        case RECORD_WAITING_START:
            break;
        case RECORD_WAITING_NEW_FILE:
        case RECORD_RUNNING:
            record_close_file();
            break;
    }

    audio_stop();
    led_record_stopped();
    data->state = RECORD_STOPPED;
    return 0;
}

int record_stop(void) {
    const struct record_config *config = &record_config;

    K_MUTEX_AUTO_LOCK(config->mutex);
    return record_stop_unlocked();
}

int record_shutdown(void) {
    const struct record_config *config = &record_config;
    struct record_data *data = &record_data;
    int ret;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (!data->init) {
        return -EALREADY;
    }

    ret = record_stop_unlocked();
    if (ret < 0) return ret;

    // Clear init to prevent any other recordings from being started
    data->init = false;
    return 0;
}