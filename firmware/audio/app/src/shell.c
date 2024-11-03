#include <math.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "audio.h"
#include "mgr.h"

static int parse_uint32(const char *str, uint32_t *u) {
    BUILD_ASSERT(sizeof(unsigned long) == sizeof(uint32_t),
                 "unsigned long is not 32-bits");
    char *endptr;
    errno = 0;
    *u = strtoul(str, &endptr, 10);
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

static int cmd_impedance(const struct shell *sh, size_t argc, char **argv) {
    int ret;

    const char *channel_str = argv[1];
    audio_channel_t channel;
    ret = audio_channel_from_string(channel_str, &channel);
    if (ret) {
        shell_error(sh, "invalid channel: %s", channel_str);
        return ret;
    }

    const char *impedance_str = argv[2];
    uint32_t impedance_ohms;
    ret = parse_uint32(impedance_str, &impedance_ohms);
    if (ret) {
        shell_error(sh, "invalid impedance: %s", impedance_str);
        return ret;
    }

    // Duplicated logic, but worth it to get better errors
    switch (impedance_ohms) {
        case 2500:
        case 10000:
        case 20000:
            break;
        default:
            shell_error(
                sh,
                "unsupported impedance; supported values: 2500, 10000, 20000");
            return -EINVAL;
    }

    ret = audio_set_impedance(channel, impedance_ohms);
    if (ret) {
        shell_error(sh, "failed to set impedance (err %d)", ret);
        return ret;
    }

    return 0;
}

SHELL_SUBCMD_ADD((zeus), impedance, NULL, "Adjust channel input impedance",
                 cmd_impedance, 3, 0);

static int channel_status(const struct shell *sh, audio_channel_t channel) {
    int32_t gain;
    uint32_t impedance;
    int ret;

    ret = audio_get_analog_gain(channel, &gain);
    if (ret) return ret;
    shell_print(sh, "   Analog gain: %5.1f dB", gain / 2.);
    ret = audio_get_digital_gain(channel, &gain);
    if (ret) return ret;
    shell_print(sh, "  Digital gain: %5.1f dB", gain / 2.);
    ret = audio_get_impedance(channel, &impedance);
    if (ret) return ret;
    shell_print(sh, "     Impedance: %5.2g kΩ", impedance / 1000.);

    return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv) {
    int ret;

    shell_print(sh, "Left");
    ret = channel_status(sh, AUDIO_CHANNEL_FRONT_LEFT);
    if (ret) return ret;

    shell_print(sh, "Right");
    ret = channel_status(sh, AUDIO_CHANNEL_FRONT_RIGHT);
    if (ret) return ret;

    return 0;
}

SHELL_SUBCMD_ADD((zeus), status, NULL, "Get ADC/recording status", cmd_status,
                 1, 0);
