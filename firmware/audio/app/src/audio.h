// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/audio/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

struct audio_block {
    uint8_t *buf;
    size_t len;
    uint32_t start_time;
    uint32_t duration;
    uint8_t bytes_per_frame;
};

int audio_init(void);

/// Power on the ADC. The I2S peripheral is always running to allow
/// synchronization. Return -EALREADY if ADC is already running.
int audio_start(void);

/// Shutdown the ADC to save power. The I2S peripheral is remaings running to
/// allow synchronization. Return -EALREADY if ADC is already powered off.
int audio_stop(void);

/// Convert the name of a channel into its channel enum value. Return 0 if
/// successful, or -1 if the name does not match any supported channel.
int audio_channel_from_string(const char *str, audio_channel_t *channel);

/// Get the ADC analog gain for the specified channel, in units 0f 0.5dB.
int audio_get_analog_gain(audio_channel_t channel, int32_t *gain);

/// Set and save the ADC analog gain for the specified channel, in units of
/// 0.5dB. The configured gain persists across reboots.
int audio_set_analog_gain(audio_channel_t channel, int32_t gain);

/// Get the ADC digital gain for the specified channel, in units 0f 0.5dB.
int audio_get_digital_gain(audio_channel_t channel, int32_t *gain);

/// Set and save the ADC digital gain for the specified channel, in units of
/// 0.5dB. The configured gain persists across reboots.
int audio_set_digital_gain(audio_channel_t channel, int32_t gain);

#ifdef __cplusplus
}
#endif