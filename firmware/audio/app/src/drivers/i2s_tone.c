// SPDX-License-Identifier: GPL-3.0-or-later

#define DT_DRV_COMPAT zephyr_tone_i2s

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(i2s_tone, CONFIG_I2S_LOG_LEVEL);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct i2s_tone_config {
    uint32_t frequency;
};

struct i2s_tone_data {
    enum i2s_dir dir;
    struct i2s_config cfg;
    enum i2s_state state;
    float phase;
};

static int i2s_tone_initialize(const struct device *dev) { return 0; }

static int i2s_tone_configure(const struct device *dev, enum i2s_dir dir,
                              const struct i2s_config *i2s_cfg) {
    struct i2s_tone_data *data = dev->data;

    if (data->state != I2S_STATE_READY && data->state != I2S_STATE_NOT_READY) {
        LOG_ERR("Cannot configure in state: %d", data->state);
        return -EINVAL;
    }

    switch (dir) {
        case I2S_DIR_RX:
            break;
        case I2S_DIR_TX:
        case I2S_DIR_BOTH:
            return -ENOSYS;
        default:
            return -EINVAL;
    }

    switch (i2s_cfg->word_size) {
        case 8:
        case 16:
        case 24:
        case 32:
            break;
        default:
            LOG_ERR("Unsupported word size: %u", i2s_cfg->word_size);
            return -EINVAL;
    }

    data->dir = dir;
    data->cfg = *i2s_cfg;
    data->state = I2S_STATE_READY;

    return 0;
}

static int i2s_tone_read(const struct device *dev, void **mem_block,
                         size_t *size) {
    const struct i2s_tone_config *cfg = dev->config;
    struct i2s_tone_data *data = dev->data;
    int ret;

    if (data->state == I2S_STATE_NOT_READY || data->dir != I2S_DIR_RX) {
        LOG_ERR("Device is not configured for RX");
        return -EIO;
    }

    ret = k_mem_slab_alloc(data->cfg.mem_slab, mem_block, K_NO_WAIT);
    if (ret < 0) return ret;
    *size = data->cfg.block_size;

    uint8_t *buffer = *mem_block;
    uint8_t sample_bytes = data->cfg.word_size / 8;
    uint8_t frame_bytes = sample_bytes * data->cfg.channels;
    float cycle_samples = (float)data->cfg.frame_clk_freq / cfg->frequency;
    __ASSERT(*size % frame_bytes == 0, "Frames don't fit neatly in buffer");
    uint32_t buffer_frames = *size / frame_bytes;

    int32_t amplitude = ((1 << data->cfg.word_size) - 1) / 2;

    for (uint32_t i = 0; i < buffer_frames; ++i) {
        float phase =
            data->phase + (float)i / cycle_samples * (2 * (float)M_PI);
        int32_t val = sinf(phase) * amplitude;
        val = sys_cpu_to_le32(val);
        for (uint8_t ch = 0; ch < data->cfg.channels; ++ch) {
            memcpy(buffer + i * frame_bytes + ch * sample_bytes, &val,
                   sample_bytes);
        }
    }
    // Save phase to start next buffer
    float _whole;
    data->phase =
        modff(buffer_frames / cycle_samples, &_whole) * (2 * (float)M_PI);

    k_sleep(K_MSEC(100));

    return 0;
}

static int i2s_tone_write(const struct device *dev, void *mem_block,
                          size_t size) {
    return -ENOSYS;
}

static int i2s_tone_trigger(const struct device *dev, enum i2s_dir dir,
                            enum i2s_trigger_cmd cmd) {
    struct i2s_tone_data *data = dev->data;

    if (data->dir != dir) {
        return -EIO;
    }

    switch (cmd) {
        case I2S_TRIGGER_START:
            return 0;
        default:
            return -ENOSYS;
    }
}

static const struct i2s_driver_api i2s_tone_driver_api = {
    .configure = i2s_tone_configure,
    .read = i2s_tone_read,
    .write = i2s_tone_write,
    .trigger = i2s_tone_trigger,
};

#define I2S_TONE_INIT(inst)                                               \
    static struct i2s_tone_data i2s_tone_data_##inst = {                  \
        .state = I2S_STATE_NOT_READY,                                     \
    };                                                                    \
    static const struct i2s_tone_config i2s_tone_config_##inst = {        \
        .frequency = DT_INST_PROP(inst, frequency),                       \
    };                                                                    \
    DEVICE_DT_INST_DEFINE(inst, i2s_tone_initialize, NULL,                \
                          &i2s_tone_data_##inst, &i2s_tone_config_##inst, \
                          POST_KERNEL, CONFIG_I2S_INIT_PRIORITY,          \
                          &i2s_tone_driver_api);

DT_INST_FOREACH_STATUS_OKAY(I2S_TONE_INIT)