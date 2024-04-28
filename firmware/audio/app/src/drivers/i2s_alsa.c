// SPDX-License-Identifier: GPL-3.0-or-later

#define DT_DRV_COMPAT linux_alsa_i2s

#define _POSIX_C_SOURCE 200809L

#include <alsa/asoundlib.h>
#include <errno.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2s_alsa, CONFIG_I2S_LOG_LEVEL);

struct i2s_alsa_config {};

struct i2s_alsa_data {
    snd_pcm_t *handle;
    enum i2s_dir dir;
    struct i2s_config cfg;
    enum i2s_state state;
};

static int i2s_alsa_initialize(const struct device *dev) { return 0; }

static int i2s_alsa_configure(const struct device *dev, enum i2s_dir dir,
                              const struct i2s_config *i2s_cfg) {
    struct i2s_alsa_data *data = dev->data;
    int ret;

    if (data->state != I2S_STATE_READY && data->state != I2S_STATE_NOT_READY) {
        LOG_ERR("Cannot configure in state: %d", data->state);
        return -EINVAL;
    }

    snd_pcm_stream_t stream;
    switch (dir) {
        case I2S_DIR_RX:
            stream = SND_PCM_STREAM_CAPTURE;
            break;
        case I2S_DIR_TX:
            stream = SND_PCM_STREAM_PLAYBACK;
            break;
        case I2S_DIR_BOTH:
            return -ENOSYS;
        default:
            return -EINVAL;
    }

    snd_pcm_format_t format;
    switch (i2s_cfg->word_size) {
        case 8:
            format = SND_PCM_FORMAT_S8;
            break;
        case 16:
            format = SND_PCM_FORMAT_S16_LE;
            break;
        case 24:
            format = SND_PCM_FORMAT_S24_LE;
            break;
        case 32:
            format = SND_PCM_FORMAT_S32_LE;
            break;
        default:
            LOG_ERR("Unsupported word size: %u", i2s_cfg->word_size);
            return -EINVAL;
    }

    if (data->handle) {
        snd_pcm_close(data->handle);
        data->handle = NULL;
    }
    data->state = I2S_STATE_NOT_READY;

    ret = snd_pcm_open(&data->handle, "default", stream, SND_PCM_NONBLOCK);
    if (ret < 0) {
        data->handle = NULL;
        return ret;
    }

    data->dir = dir;

    ret = snd_pcm_set_params(data->handle, format,
                             SND_PCM_ACCESS_RW_INTERLEAVED, i2s_cfg->channels,
                             i2s_cfg->frame_clk_freq, 1, 500000);
    if (ret < 0) return ret;

    data->cfg = *i2s_cfg;
    data->state = I2S_STATE_READY;

    return 0;
}

static int i2s_alsa_read(const struct device *dev, void **mem_block,
                         size_t *size) {
    struct i2s_alsa_data *data = dev->data;
    int err;

    if (!data->handle || data->dir != I2S_DIR_RX) {
        LOG_ERR("Device is not configured for RX");
        return -EIO;
    }

    err = k_mem_slab_alloc(data->cfg.mem_slab, mem_block, K_NO_WAIT);
    if (err < 0) return err;

    char *buffer = *mem_block;
    size_t frame_bytes = data->cfg.channels * (data->cfg.word_size / 8);
    snd_pcm_uframes_t buffer_frames = data->cfg.block_size / frame_bytes;
    while (buffer_frames > 0) {
        snd_pcm_sframes_t frames =
            snd_pcm_readi(data->handle, buffer, buffer_frames);
        if (frames >= 0) {
            buffer_frames -= frames;
            buffer += frames * frame_bytes;
        } else if (frames != -EAGAIN) {
            data->state = I2S_STATE_ERROR;
            return frames;
        } else {
            // k_cpu_idle();
            // LOG_INF("a");
            k_yield();
            // k_sleep(K_MSEC(3));
        }
    }

    *size = data->cfg.block_size;

    // k_sleep(K_MSEC(5));
    printf("return\n");
    return 0;
}

static int i2s_alsa_write(const struct device *dev, void *mem_block,
                          size_t size) {
    struct i2s_alsa_data *data = dev->data;

    if (!data->handle || data->dir != I2S_DIR_TX) {
        LOG_ERR("Device is not configured for TX");
        return -EIO;
    }

    return -ENOSYS;
}

static int i2s_alsa_trigger(const struct device *dev, enum i2s_dir dir,
                            enum i2s_trigger_cmd cmd) {
    struct i2s_alsa_data *data = dev->data;

    if (!data->handle || data->dir != dir) {
        return -EIO;
    }

    switch (cmd) {
        case I2S_TRIGGER_START:
            return snd_pcm_start(data->handle);
        default:
            return -ENOSYS;
    }
}

static const struct i2s_driver_api i2s_alsa_driver_api = {
    .configure = i2s_alsa_configure,
    .read = i2s_alsa_read,
    .write = i2s_alsa_write,
    .trigger = i2s_alsa_trigger,
};

#define I2S_ALSA_INIT(inst)                                               \
    static struct i2s_alsa_data i2s_alsa_data_##inst = {                  \
        .state = I2S_STATE_NOT_READY,                                     \
    };                                                                    \
    static const struct i2s_alsa_config i2s_alsa_config_##inst;           \
    DEVICE_DT_INST_DEFINE(inst, i2s_alsa_initialize, NULL,                \
                          &i2s_alsa_data_##inst, &i2s_alsa_config_##inst, \
                          POST_KERNEL, CONFIG_I2S_INIT_PRIORITY,          \
                          &i2s_alsa_driver_api);

DT_INST_FOREACH_STATUS_OKAY(I2S_ALSA_INIT)