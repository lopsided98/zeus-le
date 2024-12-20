/*
 * Copyright 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_bq2515x_regulator

#include <errno.h>

#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/linear_range.h>
#include <zephyr/sys/util.h>
#include <zeus/drivers/mfd/bq2515x.h>
#include <zeus/dt-bindings/regulator/bq2515x.h>

static const struct linear_range ldo_range = LINEAR_RANGE_INIT(600000, 100000U, 0, 31);

struct regulator_bq2515x_config {
	const struct device *mfd;
};

struct regulator_bq2515x_ldo_data {
	struct regulator_common_data common;
};

struct regulator_bq2515x_ldo_config {
	struct regulator_common_config common;
	const struct device *mfd;
};

static int regulator_bq2515x_ship_mode(const struct device *dev)
{
	const struct regulator_bq2515x_config *config = dev->config;

	return mfd_bq2515x_reg_update(config->mfd, BQ2515X_ICCTRL0_ADDR,
				      BQ2515X_ICCTRL0_EN_SHIP_MODE, BQ2515X_ICCTRL0_EN_SHIP_MODE);
}

static struct regulator_parent_driver_api parent_api = {
	.ship_mode = regulator_bq2515x_ship_mode,
};

static int regulator_bq2515x_init(const struct device *dev)
{
	const struct regulator_bq2515x_config *config = dev->config;

	if (!device_is_ready(config->mfd)) {
		return -ENODEV;
	}

	return 0;
}

static int regulator_bq2515x_ldo_enable(const struct device *dev)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;

	return mfd_bq2515x_reg_update(config->mfd, BQ2515X_LDOCTRL_ADDR, BQ2515X_LDOCTRL_EN_LS_LDO,
				      BQ2515X_LDOCTRL_EN_LS_LDO);
}

static int regulator_bq2515x_ldo_disable(const struct device *dev)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;

	return mfd_bq2515x_reg_update(config->mfd, BQ2515X_LDOCTRL_ADDR, BQ2515X_LDOCTRL_EN_LS_LDO,
				      0);
}

static int regulator_bq2515x_ldo_set_mode(const struct device *dev, regulator_mode_t mode)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;
	uint8_t val;

	/* FIXME: the regulator must be disabled to switch modes. How should we
	 * handle this? */

	if (mode == BQ2515X_REGULATOR_MODE_LDO) {
		val = 0;
	} else if (mode == BQ2515X_REGULATOR_MODE_LOAD_SWITCH) {
		val = BQ2515X_LDOCTRL_LDO_SWITCH_CONFIG;
	} else {
		return -ENOTSUP;
	}

	return mfd_bq2515x_reg_update(config->mfd, BQ2515X_LDOCTRL_ADDR,
				      BQ2515X_LDOCTRL_LDO_SWITCH_CONFIG, val);
}

static int regulator_bq2515x_ldo_get_mode(const struct device *dev, regulator_mode_t *mode)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;
	uint8_t ldoctrl;
	int ret;

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_LDOCTRL_ADDR, &ldoctrl);
	if (ret < 0) {
		return ret;
	}

	if (ldoctrl & BQ2515X_LDOCTRL_LDO_SWITCH_CONFIG) {
		*mode = BQ2515X_REGULATOR_MODE_LOAD_SWITCH;
	} else {
		*mode = BQ2515X_REGULATOR_MODE_LDO;
	}

	return 0;
}

static inline unsigned int regulator_bq2515x_ldo_count_voltages(const struct device *dev)
{
	return linear_range_values_count(&ldo_range);
}

static int regulator_bq2515x_ldo_list_voltage(const struct device *dev, unsigned int idx,
					      int32_t *volt_uv)
{
	return linear_range_get_value(&ldo_range, idx, volt_uv);
}

static int regulator_bq2515x_ldo_set_voltage(const struct device *dev, int32_t min_uv,
					     int32_t max_uv)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;
	uint16_t idx;
	int ret;

	ret = linear_range_get_win_index(&ldo_range, min_uv, max_uv, &idx);
	if (ret < 0) {
		return ret;
	}

	return mfd_bq2515x_reg_update(config->mfd, BQ2515X_LDOCTRL_ADDR, BQ2515X_LDOCTRL_VLDO,
				      FIELD_PREP(BQ2515X_LDOCTRL_VLDO, idx));
}

static int regulator_bq2515x_ldo_get_voltage(const struct device *dev, int32_t *volt_uv)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;
	uint8_t ldoctrl;
	uint8_t idx;
	int ret;

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_LDOCTRL_ADDR, &ldoctrl);
	if (ret < 0) {
		return ret;
	}

	idx = FIELD_GET(BQ2515X_LDOCTRL_VLDO, ldoctrl);

	return linear_range_get_value(&ldo_range, idx, volt_uv);
}

static int regulator_bq2515x_ldo_get_error_flags(const struct device *dev,
						 regulator_error_flags_t *flags)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;
	uint8_t flag3;
	int ret;

	*flags = 0;

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_FLAG3_ADDR, &flag3);
	if (ret < 0) {
		return ret;
	}

	if (flag3 & BQ2515X_FLAG3_LDO_OCP_FAULT_FLAG) {
		*flags |= REGULATOR_ERROR_OVER_CURRENT;
	}
	return 0;
}

static const struct regulator_driver_api ldo_api = {
	.enable = regulator_bq2515x_ldo_enable,
	.disable = regulator_bq2515x_ldo_disable,
	.set_mode = regulator_bq2515x_ldo_set_mode,
	.get_mode = regulator_bq2515x_ldo_get_mode,
	.set_voltage = regulator_bq2515x_ldo_set_voltage,
	.get_voltage = regulator_bq2515x_ldo_get_voltage,
	.list_voltage = regulator_bq2515x_ldo_list_voltage,
	.count_voltages = regulator_bq2515x_ldo_count_voltages,
	.get_error_flags = regulator_bq2515x_ldo_get_error_flags,
};

static int regulator_bq2515x_ldo_init(const struct device *dev)
{
	const struct regulator_bq2515x_ldo_config *config = dev->config;
	uint8_t val;
	int ret;

	if (!device_is_ready(config->mfd)) {
		return -ENODEV;
	}

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_LDOCTRL_ADDR, &val);
	if (ret) {
		return ret;
	}

	regulator_common_data_init(dev);

	return regulator_common_init(dev, val & BQ2515X_LDOCTRL_EN_LS_LDO);
}

#define REGULATOR_BQ2515X_LDO_DEFINE(node_id, id)                                                  \
	static struct regulator_bq2515x_ldo_data data_##id;                                        \
                                                                                                   \
	static const struct regulator_bq2515x_ldo_config config_##id = {                           \
		.common = REGULATOR_DT_COMMON_CONFIG_INIT(node_id),                                \
		.mfd = DEVICE_DT_GET(DT_GPARENT(node_id)),                                         \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_DEFINE(node_id, regulator_bq2515x_ldo_init, NULL, &data_##id, &config_##id,      \
			 POST_KERNEL, CONFIG_REGULATOR_BQ2515X_INIT_PRIORITY, &ldo_api);

#define REGULATOR_BQ2515X_DEFINE_COND(inst, child, fn)                                             \
	COND_CODE_1(DT_NODE_EXISTS(DT_INST_CHILD(inst, child)),                                    \
		    (fn(DT_INST_CHILD(inst, child), child##inst)), ())

#define REGULATOR_BQ2515X_DEFINE_ALL(inst)                                                         \
	static const struct regulator_bq2515x_config config_##inst = {                             \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, regulator_bq2515x_init, NULL, NULL, &config_##inst,            \
			      POST_KERNEL, CONFIG_REGULATOR_BQ2515X_INIT_PRIORITY, &parent_api);   \
                                                                                                   \
	REGULATOR_BQ2515X_DEFINE_COND(inst, ldo, REGULATOR_BQ2515X_LDO_DEFINE)
// TODO: use a different init priority?

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_BQ2515X_DEFINE_ALL)
