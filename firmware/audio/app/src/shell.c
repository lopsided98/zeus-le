#include <math.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "audio.h"
#include "mgr.h"

static int parse_float(const char *str, float *f) {
    char *endptr;
    errno = 0;
    *f = strtof(str, &endptr);
    if (str == endptr) {
        // Did not convert anything
        return -EINVAL;
    } else if (errno != 0) {
        return -errno;
    }
    if (*endptr) {
        // More characters after the end of the number
        return -EINVAL;
    }
    return 0;
}

static int cmd_pair(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start pairing command");
    return mgr_pair_start();
}

SHELL_SUBCMD_ADD((zeus), pair, NULL, "Pair with a central node", cmd_pair, 1,
                 0);

static int cmd_analog_gain(const struct shell *sh, size_t argc, char **argv) {
    int ret;

    const char *channel_str = argv[1];
    audio_channel_t channel;
    ret = audio_channel_from_string(channel_str, &channel);
    if (ret) {
        shell_error(sh, "invalid channel: %s", channel_str);
        return ret;
    }

    const char *gain_str = argv[2];
    float gain;
    ret = parse_float(gain_str, &gain);
    if (ret) {
        shell_error(sh, "invalid gain: %s", gain_str);
        return ret;
    }

    ret = audio_set_analog_gain(channel, (int32_t)roundf(gain * 2));
    if (ret) {
        shell_error(sh, "failed to set gain (err %d)", ret);
        return ret;
    }

    return 0;
}

SHELL_SUBCMD_ADD((zeus), analog_gain, NULL, "Adjust channel analog gain",
                 cmd_analog_gain, 3, 0);

static int cmd_digital_gain(const struct shell *sh, size_t argc, char **argv) {
    int ret;

    const char *channel_str = argv[1];
    audio_channel_t channel;
    ret = audio_channel_from_string(channel_str, &channel);
    if (ret) {
        shell_error(sh, "invalid channel: %s", channel_str);
        return ret;
    }

    const char *gain_str = argv[2];
    float gain;
    ret = parse_float(gain_str, &gain);
    if (ret) {
        shell_error(sh, "invalid gain: %s", gain_str);
        return ret;
    }

    ret = audio_set_digital_gain(channel, (int32_t)roundf(gain * 2));
    if (ret) {
        shell_error(sh, "failed to set gain (err %d)", ret);
        return ret;
    }

    return 0;
}

SHELL_SUBCMD_ADD((zeus), digital_gain, NULL, "Adjust channel digital gain",
                 cmd_digital_gain, 3, 0);

static int cmd_status(const struct shell *sh, size_t argc, char **argv) {
    int32_t gain;
    int ret;

    shell_print(sh, "Left");
    ret = audio_get_analog_gain(AUDIO_CHANNEL_FRONT_LEFT, &gain);
    if (ret) return ret;
    shell_print(sh, "   Analog gain: %5.1f dB", gain / 2.);
    ret = audio_get_digital_gain(AUDIO_CHANNEL_FRONT_LEFT, &gain);
    if (ret) return ret;
    shell_print(sh, "  Digital gain: %5.1f dB", gain / 2.);

    shell_print(sh, "Right");
    ret = audio_get_analog_gain(AUDIO_CHANNEL_FRONT_RIGHT, &gain);
    if (ret) return ret;
    shell_print(sh, "   Analog gain: %5.1f dB", gain / 2.);
    ret = audio_get_digital_gain(AUDIO_CHANNEL_FRONT_RIGHT, &gain);
    if (ret) return ret;
    shell_print(sh, "  Digital gain: %5.1f dB", gain / 2.);

    return 0;
}

SHELL_SUBCMD_ADD((zeus), status, NULL, "Get ADC/recording status", cmd_status,
                 1, 0);
