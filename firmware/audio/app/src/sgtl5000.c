// SPDX-License-Identifier: GPL-3.0-or-later

#define DT_DRV_COMPAT nxp_sgtl5000

#include "sgtl5000.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "input_codec.h"

#define LOG_LEVEL CONFIG_AUDIO_CODEC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sgtl5000);

#define CODEC_VOLUME_MAX 45 /* +22.5 dB */
#define CODEC_VOLUME_MIN 0  /* +0.0 dB */

struct codec_driver_config {
    struct i2c_dt_spec bus;
    uint8_t micbias_resistor_k_ohms;
    uint16_t micbias_voltage_m_volts;
};

struct codec_driver_data {
    enum input_codec_source source;
    bool muted;

    uint16_t chip_ana_power_base;
};

static int codec_set_source(const struct device *dev, audio_channel_t channel,
                            enum input_codec_source source);
static int codec_set_volume(const struct device *dev, audio_channel_t channel,
                            int vol);
static int codec_set_mute(const struct device *dev, audio_channel_t channel,
                          bool mute);

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
static void codec_read_all_regs(const struct device *dev);
#define CODEC_DUMP_REGS(dev) codec_read_all_regs((dev))
#else
#define CODEC_DUMP_REGS(dev)
#endif

static int codec_write_reg(const struct device *dev, uint16_t addr,
                           uint16_t val) {
    const struct codec_driver_config *const dev_cfg = dev->config;

    uint16_t buf[] = {
        sys_cpu_to_be16(addr),
        sys_cpu_to_be16(val),
    };
    int ret = i2c_write_dt(&dev_cfg->bus, (uint8_t *)buf, sizeof(buf));
    if (ret < 0) return ret;

    LOG_DBG("WR REG:0x%04" PRIx16 " VAL:0x%04" PRIx16, addr, val);
    return 0;
}

static int codec_read_reg(const struct device *dev, uint16_t addr,
                          uint16_t *val) {
    const struct codec_driver_config *const dev_cfg = dev->config;

    addr = sys_cpu_to_be16(addr);
    int ret = i2c_write_read_dt(&dev_cfg->bus, &addr, sizeof(addr), val,
                                sizeof(*val));
    if (ret < 0) return ret;
    *val = sys_be16_to_cpu(*val);

    LOG_DBG("RD REG:%04" PRIx16 " VAL:0x%04" PRIx16, addr, *val);
    return 0;
}

static int codec_write_source_and_mute(const struct device *dev,
                                       enum input_codec_source source,
                                       bool mute) {
    struct codec_driver_data *const dev_data = dev->data;

    uint16_t val = CHIP_ANA_CTRL_MUTE_LO | CHIP_ANA_CTRL_MUTE_HP |
                   CHIP_ANA_CTRL_EN_ZCD_ADC;
    if (source == INPUT_CODEC_SOURCE_LINE_IN) {
        val |= CHIP_ANA_CTRL_SELECT_ADC;
    }
    if (mute) {
        val |= CHIP_ANA_CTRL_MUTE_ADC;
    }

    int ret = codec_write_reg(dev, CHIP_ANA_CTRL, val);
    if (ret < 0) return ret;

    dev_data->muted = mute;
    dev_data->source = source;
    return 0;
}

static int codec_initialize(const struct device *dev) {
    const struct codec_driver_config *const dev_cfg = dev->config;

    if (!device_is_ready(dev_cfg->bus.bus)) {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    return 0;
}

static int codec_configure_power(const struct device *dev,
                                 struct audio_codec_cfg *cfg) {
    /* Assumes 3.3V VDDIO and VDDA supplies and external VDDD supply */
    struct codec_driver_data *const dev_data = dev->data;
    int ret;
    uint16_t val;

    /* Disable everything by default */
    val = 0;
    ret = codec_write_reg(dev, CHIP_DIG_POWER, val);
    if (ret < 0) return ret;

    val = 0;
    if (cfg->dai_cfg.i2s.channels == 1) {
        val |= CHIP_ANA_POWER_ADC_MONO;
    }
    ret = codec_write_reg(dev, CHIP_ANA_POWER, val);
    if (ret < 0) return ret;
    /* Base CHIP_ANA_POWER configuration to modify when starting ADC */
    dev_data->chip_ana_power_base = val;

    /* Force charge pump supply to VDDIO */
    val = CHIP_LINREG_CTRL_VDDC_ASSN_OVRD | CHIP_LINREG_CTRL_VDDC_MAN_ASSN;
    ret = codec_write_reg(dev, CHIP_LINREG_CTRL, val);
    if (ret < 0) return ret;

    val = FIELD_PREP(CHIP_REF_CTRL_VAG_VAL, 0x1F /* 1.575 V */) |
          CHIP_REF_CTRL_SMALL_POP;
    ret = codec_write_reg(dev, CHIP_REF_CTRL, val);
    if (ret < 0) return ret;

    ret = codec_write_reg(dev, CHIP_REF_CTRL, val);
    if (ret < 0) return ret;

    return 0;
}

static int codec_configure_clocks(const struct device *dev,
                                  struct audio_codec_cfg *cfg) {
    struct i2s_config *i2s = &cfg->dai_cfg.i2s;

    LOG_DBG("MCLK %u Hz PCM Rate: %u Hz", cfg->mclk_freq, i2s->frame_clk_freq);

    uint8_t sys_fs_div;
    uint32_t sys_fs;
    switch (i2s->frame_clk_freq) {
        case AUDIO_PCM_RATE_8K:
            sys_fs = 32000;
            sys_fs_div = 4;
            break;
        case AUDIO_PCM_RATE_16K:
            sys_fs = 32000;
            sys_fs_div = 2;
            break;
        case AUDIO_PCM_RATE_24K:
            sys_fs = 48000;
            sys_fs_div = 2;
            break;
        case AUDIO_PCM_RATE_32K:
            sys_fs = 32000;
            sys_fs_div = 1;
            break;
        case AUDIO_PCM_RATE_44P1K:
            sys_fs = 44100;
            sys_fs_div = 1;
            break;
        case AUDIO_PCM_RATE_48K:
            sys_fs = 48000;
            sys_fs_div = 1;
            break;
        case AUDIO_PCM_RATE_96K:
            sys_fs = 96000;
            sys_fs_div = 1;
            break;
        default:
            LOG_ERR("Unsupported PCM sample rate: %" PRIu32 " Hz",
                    i2s->frame_clk_freq);
            return -EINVAL;
    }
    uint16_t val = FIELD_PREP(CHIP_CLK_CTRL_RATE_MODE, sys_fs_div / 2);

    uint8_t sys_fs_reg;
    switch (sys_fs) {
        case 32000:
            sys_fs_reg = CHIP_CLK_CTRL_SYS_FS_32_KHZ;
            break;
        case 44100:
            sys_fs_reg = CHIP_CLK_CTRL_SYS_FS_44_1_KHZ;
            break;
        case 48000:
            sys_fs_reg = CHIP_CLK_CTRL_SYS_FS_48_KHZ;
            break;
        case 96000:
            sys_fs_reg = CHIP_CLK_CTRL_SYS_FS_96_KHZ;
            break;
    }
    val |= FIELD_PREP(CHIP_CLK_CTRL_SYS_FS, sys_fs_reg);

    uint8_t mclk_freq;
    if (cfg->mclk_freq == 256 * sys_fs) {
        mclk_freq = CHIP_CLK_CTRL_MCLK_FREQ_256_FS;
    } else if (cfg->mclk_freq == 384 * sys_fs) {
        mclk_freq = CHIP_CLK_CTRL_MCLK_FREQ_384_FS;
    } else if (cfg->mclk_freq == 512 * sys_fs) {
        mclk_freq = CHIP_CLK_CTRL_MCLK_FREQ_512_FS;
    } else {
        LOG_ERR("Unsupported MCLK rate: %" PRIu32
                " Hz (PLL is not currently supported)",
                cfg->mclk_freq);
        return -EINVAL;
    }
    val |= FIELD_PREP(CHIP_CLK_CTRL_MCLK_FREQ, mclk_freq);

    return codec_write_reg(dev, CHIP_CLK_CTRL, val);
}

static int codec_configure_dai(const struct device *dev, audio_dai_cfg_t *cfg) {
    /* configure I2S interface */

    uint16_t val = FIELD_PREP(CHIP_I2S_CTRL_I2S_MODE,
                              CHIP_I2S_CTRL_I2S_MODE_I2S_LEFT_JUSTIFIED);
    if ((cfg->i2s.options & I2S_OPT_BIT_CLK_SLAVE) &&
        (cfg->i2s.options & I2S_OPT_FRAME_CLK_SLAVE)) {
        val |= CHIP_I2S_CTRL_MS;
    } else if ((cfg->i2s.options & I2S_OPT_BIT_CLK_SLAVE) ||
               (cfg->i2s.options & I2S_OPT_FRAME_CLK_SLAVE)) {
        LOG_ERR("Master/slave status for bit clock and frame clock must match");
        return -EINVAL;
    }

    if (cfg->i2s.format & I2S_FMT_BIT_CLK_INV) {
        val |= CHIP_I2S_CTRL_SCLK_INV;
    }

    if (cfg->i2s.format & I2S_FMT_FRAME_CLK_INV) {
        val |= CHIP_I2S_CTRL_LRPOL;
    }

    uint8_t dlen;
    switch (cfg->i2s.word_size) {
        case AUDIO_PCM_WIDTH_16_BITS:
            dlen = CHIP_I2S_CTRL_DLEN_16;
            break;
        case AUDIO_PCM_WIDTH_20_BITS:
            dlen = CHIP_I2S_CTRL_DLEN_20;
            break;
        case AUDIO_PCM_WIDTH_24_BITS:
            dlen = CHIP_I2S_CTRL_DLEN_24;
            break;
        case AUDIO_PCM_WIDTH_32_BITS:
            dlen = CHIP_I2S_CTRL_DLEN_32;
            break;
        default:
            LOG_ERR("Unsupported PCM sample bit width %u", cfg->i2s.word_size);
            return -EINVAL;
    }
    val |= FIELD_PREP(CHIP_I2S_CTRL_DLEN, dlen);

    /*
     * Must match I2S driver config, but this is not known here, so just use the
     * minimum required.
     */
    if (cfg->i2s.word_size == AUDIO_PCM_WIDTH_16_BITS) {
        val |= CHIP_I2S_CTRL_SCLKFREQ;
    }

    return codec_write_reg(dev, CHIP_I2S_CTRL, val);
}

static int codec_configure_input(const struct device *dev) {
    const struct codec_driver_config *const dev_cfg = dev->config;

    int ret = codec_write_source_and_mute(dev, INPUT_CODEC_SOURCE_MIC, true);
    if (ret < 0) return ret;

    uint8_t bias_resistor = LOG2CEIL(dev_cfg->micbias_resistor_k_ohms);
    uint8_t bias_volt = (dev_cfg->micbias_voltage_m_volts - 1250) / 250;
    uint16_t val = FIELD_PREP(CHIP_MIC_CTRL_BIAS_RESISTOR, bias_resistor) |
                   FIELD_PREP(CHIP_MIC_CTRL_BIAS_VOLT, bias_volt);
    ret = codec_write_reg(dev, CHIP_MIC_CTRL, val);
    if (ret < 0) return ret;

    val = CHIP_SSS_CTRL_DAC_SELECT_I2S_IN | CHIP_SSS_CTRL_I2S_SELECT_ADC;
    return codec_write_reg(dev, CHIP_SSS_CTRL, val);
}

static int codec_configure(const struct device *dev,
                           struct audio_codec_cfg *cfg) {
    int ret;

    if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
        LOG_ERR("dai_type must be AUDIO_DAI_TYPE_I2S");
        return -EINVAL;
    }

    /* Make sure we are talking to the right chip */
    uint16_t chip_id;
    ret = codec_read_reg(dev, CHIP_ID_ADDR, &chip_id);
    if (ret < 0) return ret;

    uint8_t partid = FIELD_GET(CHIP_ID_PARTID, chip_id);
    if (partid != CHIP_ID_PARTID_SGTL5000) {
        LOG_ERR("Unexpected part ID: %02" PRIx8 " != %02" PRIx8, partid,
                CHIP_ID_PARTID_SGTL5000);
        return -EIO;
    }

    ret = codec_configure_power(dev, cfg);
    if (ret < 0) return ret;

    ret = codec_configure_clocks(dev, cfg);
    if (ret < 0) return ret;

    ret = codec_configure_dai(dev, &cfg->dai_cfg);
    if (ret < 0) return ret;

    ret = codec_configure_input(dev);
    if (ret < 0) return ret;

    CODEC_DUMP_REGS(dev);
    LOG_INF("SGTL5000 configured");

    return 0;
}

static int codec_start_input(const struct device *dev) {
    struct codec_driver_data *const dev_data = dev->data;
    /* Turn on analog blocks */
    uint16_t val = dev_data->chip_ana_power_base |
                   CHIP_ANA_POWER_REFTOP_POWERUP | CHIP_ANA_POWER_ADC_POWERUP;
    int ret = codec_write_reg(dev, CHIP_ANA_POWER, val);
    if (ret < 0) return ret;

    /* Turn on digital blocks */
    val = CHIP_DIG_POWER_ADC_POWERUP | CHIP_DIG_POWER_I2S_OUT_POWERUP;
    ret = codec_write_reg(dev, CHIP_DIG_POWER, val);
    if (ret < 0) return ret;

    /* Unmute */
    ret = codec_set_mute(dev, AUDIO_CHANNEL_ALL, false);
    if (ret < 0) return ret;

    CODEC_DUMP_REGS(dev);
    return 0;
}

static int codec_stop_input(const struct device *dev) {
    struct codec_driver_data *const dev_data = dev->data;
    /* Mute */
    int ret = codec_set_mute(dev, AUDIO_CHANNEL_ALL, false);
    if (ret < 0) return ret;

    /* Turn off digital blocks */
    ret = codec_write_reg(dev, CHIP_DIG_POWER, 0);
    if (ret < 0) return ret;

    /* Turn off analog blocks */
    ret = codec_write_reg(dev, CHIP_ANA_POWER, dev_data->chip_ana_power_base);
    if (ret < 0) return ret;

    CODEC_DUMP_REGS(dev);
    return 0;
}

static int codec_set_source(const struct device *dev, audio_channel_t channel,
                            enum input_codec_source source) {
    struct codec_driver_data *const dev_data = dev->data;
    if (channel != AUDIO_CHANNEL_ALL) {
        return -EINVAL;
    }

    if (source == dev_data->source) {
        return 0;
    }

    /* Mute ADC to avoid pop/click artifacts */
    bool was_muted = dev_data->muted;
    int ret = codec_set_mute(dev, channel, true);
    if (ret < 0) return ret;

    ret = codec_write_source_and_mute(dev, source, true);
    if (ret < 0) return ret;

    /* Restore mute state */
    if (!was_muted) {
        ret = codec_set_mute(dev, channel, false);
        if (ret < 0) return ret;
    }
    return 0;
}

static int codec_set_mute(const struct device *dev, audio_channel_t channel,
                          bool mute) {
    struct codec_driver_data *const dev_data = dev->data;
    if (channel != AUDIO_CHANNEL_ALL) {
        return -EINVAL;
    }

    if (mute == dev_data->muted) {
        return 0;
    }

    return codec_write_source_and_mute(dev, dev_data->source, mute);
}

static int codec_set_volume(const struct device *dev, audio_channel_t channel,
                            int vol) {
    if ((vol > CODEC_VOLUME_MAX) || (vol < CODEC_VOLUME_MIN)) {
        LOG_ERR("Invalid volume %d.%d dB", vol >> 1,
                ((uint32_t)vol & 1) ? 5 : 0);
        return -EINVAL;
    }

    /* Round to nearest 1.5 dB step */
    uint8_t vol_val = (vol + 1) / 3;

    uint16_t val;
    switch (channel) {
        case AUDIO_CHANNEL_FRONT_LEFT:
            val = FIELD_PREP(CHIP_ANA_ADC_CTRL_ADC_VOL_LEFT, vol_val);
            break;
        case AUDIO_CHANNEL_FRONT_RIGHT:
            val = FIELD_PREP(CHIP_ANA_ADC_CTRL_ADC_VOL_RIGHT, vol_val);
            break;
        case AUDIO_CHANNEL_ALL:
            val = FIELD_PREP(CHIP_ANA_ADC_CTRL_ADC_VOL_LEFT, vol_val) |
                  FIELD_PREP(CHIP_ANA_ADC_CTRL_ADC_VOL_RIGHT, vol_val);
            break;
        default:
            return -EINVAL;
    }

    return codec_write_reg(dev, CHIP_ANA_ADC_CTRL, val);
}

static int codec_set_property(const struct device *dev,
                              enum input_codec_property property,
                              audio_channel_t channel,
                              union input_codec_property_value val) {
    switch (property) {
        case INPUT_CODEC_PROPERTY_SOURCE:
            return codec_set_source(dev, channel, val.source);
            break;
        case INPUT_CODEC_PROPERTY_VOLUME:
            return codec_set_volume(dev, channel, val.vol);

        case INPUT_CODEC_PROPERTY_MUTE:
            return codec_set_mute(dev, channel, val.mute);

        default:
            return -EINVAL;
    }
}

static int codec_apply_properties(const struct device *dev) {
    /* nothing to do because there is nothing cached */
    return 0;
}

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
static void codec_read_all_regs(const struct device *dev) {
    uint16_t val;

    codec_read_reg(dev, CHIP_ID_ADDR, &val);
    codec_read_reg(dev, CHIP_DIG_POWER, &val);
    codec_read_reg(dev, CHIP_CLK_CTRL, &val);
    codec_read_reg(dev, CHIP_I2S_CTRL, &val);
    codec_read_reg(dev, CHIP_SSS_CTRL, &val);
    codec_read_reg(dev, CHIP_ADCDAC_CTRL, &val);
    codec_read_reg(dev, CHIP_DAC_VOL, &val);
    codec_read_reg(dev, CHIP_PAD_STRENGTH, &val);
    codec_read_reg(dev, CHIP_ANA_ADC_CTRL, &val);
    codec_read_reg(dev, CHIP_ANA_HP_CTRL, &val);
    codec_read_reg(dev, CHIP_ANA_CTRL, &val);
    codec_read_reg(dev, CHIP_LINREG_CTRL, &val);
    codec_read_reg(dev, CHIP_REF_CTRL, &val);
    codec_read_reg(dev, CHIP_MIC_CTRL, &val);
    codec_read_reg(dev, CHIP_LINE_OUT_CTRL, &val);
    codec_read_reg(dev, CHIP_LINE_OUT_VOL, &val);
    codec_read_reg(dev, CHIP_ANA_POWER, &val);
    codec_read_reg(dev, CHIP_PLL_CTRL, &val);
    codec_read_reg(dev, CHIP_CLK_TOP_CTRL, &val);
    codec_read_reg(dev, CHIP_ANA_STATUS, &val);
    codec_read_reg(dev, CHIP_SHORT_CTRL, &val);

    codec_read_reg(dev, DAP_CONTROL, &val);
    codec_read_reg(dev, DAP_PEQ, &val);
}
#endif

static const struct input_codec_api codec_driver_api = {
    .configure = codec_configure,
    .start_input = codec_start_input,
    .stop_input = codec_stop_input,
    .set_property = codec_set_property,
    .apply_properties = codec_apply_properties,
};

#define CREATE_CODEC(inst)                                                 \
    static struct codec_driver_data codec_device_data_##inst;              \
    static const struct codec_driver_config codec_device_config_##inst = { \
        .bus = I2C_DT_SPEC_INST_GET(inst),                                 \
        .micbias_resistor_k_ohms =                                         \
            DT_INST_PROP(inst, micbias_resistor_k_ohms),                   \
        .micbias_voltage_m_volts =                                         \
            DT_INST_PROP(inst, micbias_voltage_m_volts),                   \
    };                                                                     \
    DEVICE_DT_INST_DEFINE(                                                 \
        inst, codec_initialize, NULL, &codec_device_data_##inst,           \
        &codec_device_config_##inst, POST_KERNEL,                          \
        CONFIG_AUDIO_CODEC_INIT_PRIORITY, &codec_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_CODEC)