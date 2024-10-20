// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>
#include <errno.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>

/**
 * Codec properties that can be set by input_codec_set_property().
 */
enum input_codec_property {
	INPUT_CODEC_PROPERTY_SOURCE,       /**< Input source */
	INPUT_CODEC_PROPERTY_ANALOG_GAIN,  /**< Input analog gain */
	INPUT_CODEC_PROPERTY_DIGITAL_GAIN, /**< Input digital gain */
	INPUT_CODEC_PROPERTY_MUTE,         /**< Input mute/unmute */
};

/**
 * Audio input sources
 */
enum input_codec_source {
	INPUT_CODEC_SOURCE_MIC,     /**< Microphone input */
	INPUT_CODEC_SOURCE_LINE_IN, /**< Line in */
};

/**
 * Codec property values
 */
union input_codec_property_value {
	enum input_codec_source source; /**< Input source */
	int32_t gain;                   /**< Gain in 0.5dB resolution */
	bool mute;                      /**< Mute if @a true, unmute if @a false */
};

struct input_codec_api {
	int (*configure)(const struct device *dev, struct audio_codec_cfg *cfg);
	int (*start_input)(const struct device *dev);
	int (*stop_input)(const struct device *dev);
	int (*get_property)(const struct device *dev, enum input_codec_property property,
			    audio_channel_t channel, union input_codec_property_value *val);
	int (*set_property)(const struct device *dev, enum input_codec_property property,
			    audio_channel_t channel, union input_codec_property_value val);
	int (*apply_properties)(const struct device *dev);
};

/**
 * @brief Configure the audio input codec
 *
 * Configure the audio codec device according to the configuration
 * parameters provided as input
 *
 * @param dev Pointer to the device structure for codec driver instance.
 * @param cfg Pointer to the structure containing the codec configuration.
 *
 * @return 0 on success, negative error code on failure
 */
static inline int input_codec_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	const struct input_codec_api *api = (const struct input_codec_api *)dev->api;

	return api->configure(dev, cfg);
}

/**
 * @brief Set codec to start audio input
 *
 * Setup the audio codec device to start recording
 *
 * @param dev Pointer to the device structure for codec driver instance.
 *
 * @return 0 on success, negative error code on failure
 */
static inline int input_codec_start_input(const struct device *dev)
{
	const struct input_codec_api *api = (const struct input_codec_api *)dev->api;

	return api->start_input(dev);
}

/**
 * @brief Set codec to stop audio input
 *
 * Setup the audio codec device to stop recording
 *
 * @param dev Pointer to the device structure for codec driver instance.
 *
 * @return 0 on success, negative error code on failure
 */
static inline int input_codec_stop_input(const struct device *dev)
{
	const struct input_codec_api *api = (const struct input_codec_api *)dev->api;

	return api->stop_input(dev);
}

/**
 * @brief Get a codec property defined by input_codec_property
 *
 * Get a property such as volume level, clock configuration etc.
 *
 * @param dev Pointer to the device structure for codec driver instance.
 * @param property The codec property to set
 * @param channel The audio channel for which the property has to be set
 * @param val pointer to a property value of type input_codec_property_value
 *
 * @return 0 on success, negative error code on failure
 */
static inline int input_codec_get_property(const struct device *dev,
					   enum input_codec_property property,
					   audio_channel_t channel,
					   union input_codec_property_value *val)
{
	const struct input_codec_api *api = (const struct input_codec_api *)dev->api;

	return api->get_property(dev, property, channel, val);
}

/**
 * @brief Set a codec property defined by input_codec_property
 *
 * Set a property such as volume level, clock configuration etc.
 *
 * @param dev Pointer to the device structure for codec driver instance.
 * @param property The codec property to set
 * @param channel The audio channel for which the property has to be set
 * @param val pointer to a property value of type input_codec_property_value
 *
 * @return 0 on success, negative error code on failure
 */
static inline int input_codec_set_property(const struct device *dev,
					   enum input_codec_property property,
					   audio_channel_t channel,
					   union input_codec_property_value val)
{
	const struct input_codec_api *api = (const struct input_codec_api *)dev->api;

	return api->set_property(dev, property, channel, val);
}

/**
 * @brief Atomically apply any cached properties
 *
 * Following one or more invocations of input_codec_set_property, that may have
 * been cached by the driver, input_codec_apply_properties can be invoked to
 * apply all the properties as atomic as possible
 *
 * @param dev Pointer to the device structure for codec driver instance.
 *
 * @return 0 on success, negative error code on failure
 */
static inline int input_codec_apply_properties(const struct device *dev)
{
	const struct input_codec_api *api = (const struct input_codec_api *)dev->api;

	return api->apply_properties(dev);
}