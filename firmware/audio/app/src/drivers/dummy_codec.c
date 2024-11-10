// SPDX-License-Identifier: GPL-3.0-or-later

#define DT_DRV_COMPAT zephyr_dummy_codec

#include <zephyr/device.h>

#include "input_codec.h"

struct codec_driver_config {};

struct codec_driver_data {};

static int codec_initialize(const struct device *dev) { return 0; }

static int codec_configure(const struct device *dev,
                           struct audio_codec_cfg *cfg) {
    return 0;
}

static int codec_start_input(const struct device *dev) { return 0; }

static int codec_stop_input(const struct device *dev) { return 0; }

static int codec_get_property(const struct device *dev,
                              enum input_codec_property property,
                              audio_channel_t channel,
                              union input_codec_property_value *val) {
    return -ENOTSUP;
}

static int codec_set_property(const struct device *dev,
                              enum input_codec_property property,
                              audio_channel_t channel,
                              union input_codec_property_value val) {
    return -ENOTSUP;
}

static int codec_apply_properties(const struct device *dev) {
    /* nothing to do because there is nothing cached */
    return 0;
}

static const struct input_codec_api codec_driver_api = {
    .configure = codec_configure,
    .start_input = codec_start_input,
    .stop_input = codec_stop_input,
    .get_property = codec_get_property,
    .set_property = codec_set_property,
    .apply_properties = codec_apply_properties,
};

#define CREATE_CODEC(inst)                                              \
    static struct codec_driver_data codec_device_data_##inst;           \
    static const struct codec_driver_config codec_device_config_##inst; \
    DEVICE_DT_INST_DEFINE(                                              \
        inst, codec_initialize, NULL, &codec_device_data_##inst,        \
        &codec_device_config_##inst, POST_KERNEL,                       \
        CONFIG_AUDIO_CODEC_INIT_PRIORITY, &codec_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_CODEC)