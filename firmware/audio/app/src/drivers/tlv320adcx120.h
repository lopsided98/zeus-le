/*
 * Copyright (c) 2019 Intel Corporation.
 * Copyright (c) 2024 Ben Wolsieffer
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_AUDIO_TLV320ADCX120_H_
#define ZEPHYR_DRIVERS_AUDIO_TLV320ADCX120_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register addresses */
#define PAGE_CFG_ADDR	0

/* Register addresses {page, address} and fields */
#define SW_RESET_ADDR			(struct reg_addr){0, 0x01}
#define SW_RESET_ASSERT			BIT(1)

#define SLEEP_CFG_ADDR			(struct reg_addr){0, 0x02}
#define SLEEP_CFG_AREG_SELECT		BIT(7)
#define SLEEP_CFG_VREF_QCHG		GENMASK(4, 3)
#define SLEEP_CFG_VREF_QCHG_3_5_MS	0
#define SLEEP_CFG_VREF_QCHG_10_MS	1
#define SLEEP_CFG_VREF_QCHG_50_MS	2
#define SLEEP_CFG_VREF_QCHG_100_MS	3
#define SLEEP_CFG_I2C_BRDCAST_EN	BIT(2)
#define SLEEP_CFG_SLEEP_ENZ		BIT(0)

#define SHDN_CFG_ADDR			(struct reg_addr){0, 0x5}
#define SHDN_CFG_INCAP_QCHG		GENMASK(5, 4)

#define ASI_CFG0_ADDR			(struct reg_addr){0, 0x7}
#define ASI_CFG0_ASI_FORMAT		GENMASK(7, 6)
#define ASI_CFG0_ASI_FORMAT_TDM		0
#define ASI_CFG0_ASI_FORMAT_I2S		1
#define ASI_CFG0_ASI_FORMAT_LJ		2
#define ASI_CFG0_ASI_WLEN		GENMASK(5, 4)
#define ASI_CFG0_ASI_WLEN_16		0
#define ASI_CFG0_ASI_WLEN_20		1
#define ASI_CFG0_ASI_WLEN_24		2
#define ASI_CFG0_ASI_WLEN_32		3
#define ASI_CFG0_FSYNC_POL		BIT(3)
#define ASI_CFG0_BCLK_POL		BIT(2)
#define ASI_CFG0_TX_EDGE		BIT(1)
#define ASI_CFG0_TX_FILL		BIT(0)

#define ASI_CFG1_ADDR			(struct reg_addr){0, 0x8}

#define ASI_CFG2_ADDR			(struct reg_addr){0, 0x9}

#define ASI_MIX_CFG_ADDR		(struct reg_addr){0, 0xa}

#define ASI_CH_ADDR(ch)			(struct reg_addr){0, 0xb + (ch) - 1}
#define ASI_CH_SLOT			GENMASK(5, 0)

#define MST_CFG0_ADDR			(struct reg_addr){0, 0x13}
#define MST_CFG0_MST_SLV_CFG		BIT(7)
#define MST_CFG0_AUTO_CLK_CFG		BIT(6)
#define MST_CFG0_AUTO_MODE_PLL_DIS	BIT(5)
#define MST_CFG0_BCLK_FSYNC_GATE	BIT(4)
#define MST_CFG0_FS_MODE		BIT(3)
#define MST_CFG0_MCLK_FREQ_SEL		GENMASK(2, 0)
#define MST_CFG0_MCLK_FREQ_SEL_12_MHZ	0
#define MST_CFG0_MCLK_FREQ_SEL_12_288_MHZ 1
#define MST_CFG0_MCLK_FREQ_SEL_13_MHZ	2
#define MST_CFG0_MCLK_FREQ_SEL_16_MHZ	3
#define MST_CFG0_MCLK_FREQ_SEL_19_2_MHZ	4
#define MST_CFG0_MCLK_FREQ_SEL_19_68_MHZ 5
#define MST_CFG0_MCLK_FREQ_SEL_24_MHZ	6
#define MST_CFG0_MCLK_FREQ_SEL_24_576_MHZ 7

#define MST_CFG1_ADDR			(struct reg_addr){0, 0x14}

#define CH_CFG0_ADDR(ch)		(struct reg_addr){0, 0x3c + ((ch) - 1) * 5}
#define CH_CFG0_INTYP			BIT(7)
#define CH_CFG0_INSRC			GENMASK(6, 5)
#define CH_CFG0_INSRC_ANALOG_DIFF	0
#define CH_CFG0_INSRC_ANALOG_SINGLE	1
#define CH_CFG0_INSRC_PAM		2
#define CH_CFG0_DC			BIT(4)
#define CH_CFG0_IMP			GENMASK(3, 2)
#define CH_CFG0_IMP_2_5_KOHM		0
#define CH_CFG0_IMP_10_KOHM		1
#define CH_CFG0_IMP_20_KOHM		2
#define CH_CFG0_DREEN			BIT(0)

#define CH_CFG1_ADDR(ch)		(struct reg_addr){0, 0x3d + ((ch) - 1) * 5}
#define CH_CFG1_GAIN			GENMASK(7, 1)
#define CH_CFG1_GAIN_SIGN_BIT		BIT(0)

#define CH_CFG2_ADDR(ch)		(struct reg_addr){0, 0x3e + ((ch) - 1) * 5}
#define CH_CFG2_DVOL			GENMASK(7, 0)

#define CH_CFG3_ADDR(ch)		(struct reg_addr){0, 0x3f + ((ch) - 1) * 5}

#define CH_CFG4_ADDR(ch)		(struct reg_addr){0, 0x40 + ((ch) - 1) * 5}

#define IN_CH_EN_ADDR			(struct reg_addr){0, 0x73}
#define IN_CH_EN(ch)			BIT(7 - ((ch) - 1))

#define ASI_OUT_CH_EN_ADDR		(struct reg_addr){0, 0x74}
#define ASI_OUT_CH_EN(ch)		BIT(7 - ((ch) - 1))

#define PWR_CFG_ADDR			(struct reg_addr){0, 0x75}
#define PWR_CFG_MICBIAS_PDZ		BIT(7)
#define PWR_CFG_ADC_PDZ			BIT(6)
#define PWR_CFG_PLL_PDZ			BIT(5)
#define PWR_CFG_DYN_CH_PUPD_EN		BIT(4)
#define PWR_CFG_DYN_MAXCH_SEL		GENMASK(3, 2)
#define PWR_CFG_DYN_MAXCH_SEL_2		0
#define PWR_CFG_DYN_MAXCH_SEL_4		1
#define PWR_CFG_VAD_EN			BIT(0)

struct reg_addr {
	uint8_t page;     /* page number */
	uint8_t reg_addr; /* register address */
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_AUDIO_TLV320ADCX120_H_ */
