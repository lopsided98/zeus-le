// SPDX-License-Identifier: GPL-3.0-or-later

#define DT_DRV_COMPAT linux_alsa_i2s

#define _POSIX_SOURCE
#include <alsa/asoundlib.h>
#include <errno.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "input_codec.h"

LOG_MODULE_REGISTER(i2s_alsa, CONFIG_I2S_LOG_LEVEL);

struct i2s_alsa_config {};

struct i2s_alsa_data {
    snd_pcm_t *handle;
    enum i2s_dir dir;
    struct i2s_config cfg;
};

static int i2s_alsa_initialize(const struct device *dev) { return 0; }

static int i2s_alsa_configure(const struct device *dev, enum i2s_dir dir,
                              const struct i2s_config *i2s_cfg) {
    struct i2s_alsa_data *data = dev->data;
    int err;

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
    data->dir = dir;

    if (!data->handle) {
        err = snd_pcm_open(&data->handle, "default", stream, SND_PCM_NONBLOCK);
        if (err < 0) {
            data->handle = NULL;
            return err;
        }
    } else {
        snd_pcm_close(data->handle);
        data->handle = NULL;
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

    err = snd_pcm_set_params(data->handle, format,
                             SND_PCM_ACCESS_RW_INTERLEAVED, i2s_cfg->channels,
                             i2s_cfg->frame_clk_freq, 1, 500000);
    if (err < 0) return err;

    data->cfg = *i2s_cfg;

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

    void *buffer;
    err = k_mem_slab_alloc(data->cfg.mem_slab, &buffer, K_NO_WAIT);
    if (err < 0) return err;

    snd_pcm_uframes_t buffer_frames =
        data->cfg.block_size / data->cfg.channels / (data->cfg.word_size / 8);
    snd_pcm_sframes_t total_frames = 0;
    while (true) {
        snd_pcm_sframes_t frames =
            snd_pcm_readi(data->handle, buffer, buffer_frames);
        if (frames >= 0) {
            total_frames += err;
            if (total_frames >= buffer_frames) {
                break;
            }
        } else if (frames != -EAGAIN) {
            return frames;
        }
        k_yield();
    }
    return 0;
}

static int i2s_alsa_write(const struct device *dev, void *mem_block,
                          size_t size) {
    struct i2s_alsa_data *data = dev->data;

    if (!data->handle || data->dir != I2S_DIR_RX) {
        LOG_ERR("Device is not configured for RX");
        return -EIO;
    }

    return -ENOSYS;
}

static int i2s_alsa_trigger(const struct device *dev, enum i2s_dir dir,
                            enum i2s_trigger_cmd cmd) {
    return 0;
}

static const struct i2s_driver_api i2s_alsa_driver_api = {
    .configure = i2s_alsa_configure,
    .read = i2s_alsa_read,
    .write = i2s_alsa_write,
    .trigger = i2s_alsa_trigger,
};

#define I2S_ALSA_INIT(inst)                                               \
    static struct i2s_alsa_data i2s_alsa_data_##inst;                     \
    static const struct i2s_alsa_config i2s_alsa_config_##inst;           \
    DEVICE_DT_INST_DEFINE(inst, i2s_alsa_initialize, NULL,                \
                          &i2s_alsa_data_##inst, &i2s_alsa_config_##inst, \
                          POST_KERNEL, CONFIG_I2S_INIT_PRIORITY,          \
                          &i2s_alsa_driver_api);

DT_INST_FOREACH_STATUS_OKAY(I2S_ALSA_INIT)