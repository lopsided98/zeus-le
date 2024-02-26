// SPDX-License-Identifier: GPL-3.0-or-later

#define DT_DRV_COMPAT linux_alsa_i2s

#include <errno.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>

#include "input_codec.h"

LOG_MODULE_REGISTER(i2s_alsa, CONFIG_I2S_LOG_LEVEL);

struct i2s_alsa_config {};

struct i2s_alsa_data {};

static int i2s_alsa_initialize(const struct device *dev) { return 0; }

static int i2s_alsa_configure(const struct device *dev, enum i2s_dir dir,
                              const struct i2s_config *i2s_cfg) {
    return -ENOSYS;
}

static int i2s_alsa_read(const struct device *dev, void **mem_block,
                         size_t *size) {
    return -ENOSYS;
}

static int i2s_alsa_write(const struct device *dev, void *mem_block,
                          size_t size) {
    return -ENOSYS;
}

static int i2s_alsa_trigger(const struct device *dev, enum i2s_dir dir,
                            enum i2s_trigger_cmd cmd) {
    return -ENOSYS;
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