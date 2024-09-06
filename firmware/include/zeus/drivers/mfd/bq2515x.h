/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * Copyright (c) 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_BQ2515X_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_BQ2515X_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup mdf_interface_bq2515x MFD BQ2515X Interface
 * @ingroup mfd_interfaces
 * @{
 */

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define BQ2515X_STAT0_ADDR                 0x00
#define BQ2515X_STAT0_CHRG_CV_STAT         BIT(6)
#define BQ2515X_STAT0_CHARGE_DONE_STAT     BIT(5)
#define BQ2515X_STAT0_IINLIM_ACTIVE_STAT   BIT(4)
#define BQ2515X_STAT0_VDPPM_ACTIVE_STAT    BIT(3)
#define BQ2515X_STAT0_VINDPM_ACTIVE_STAT   BIT(2)
#define BQ2515X_STAT0_THERMREG_ACTIVE_STAT BIT(1)
#define BQ2515X_STAT0_VIN_PGOOD_STAT       BIT(0)

#define BQ2515X_FLAG0_ADDR 0x03

#define BQ2515X_FLAG3_ADDR               0x06
#define BQ2515X_FLAG3_LDO_OCP_FAULT_FLAG BIT(4)

#define BQ2515X_MASK0_ADDR 0x07

#define BQ2515X_ICHG_CTRL_ADDR 0x13

#define BQ2515X_PCHRGCTRL_ADDR          0x14
#define BQ2515X_PCHRGCTRL_ICHARGE_RANGE BIT(7)
#define BQ2515X_PCHRGCTRL_IPRECHG       GENMASK(4, 0)

#define BQ2515X_ILIMCTRL_ADDR 0x19
#define BQ2515X_ILIMCTRL_ILIM GENMASK(2, 0)

#define BQ2515X_LDOCTRL_ADDR              0x1d
#define BQ2515X_LDOCTRL_EN_LS_LDO         BIT(7)
#define BQ2515X_LDOCTRL_VLDO              GENMASK(6, 2)
#define BQ2515X_LDOCTRL_LDO_SWITCH_CONFIG BIT(1)

#define BQ2515X_ICCTRL0_ADDR             0x35
#define BQ2515X_ICCTRL0_EN_SHIP_MODE     BIT(7)
#define BQ2515X_ICCTRL0_AUTOWAKE         GENMASK(5, 4)
#define BQ2515X_ICCTRL0_AUTOWAKE_0_6_SEC 0x0
#define BQ2515X_ICCTRL0_AUTOWAKE_1_2_SEC 0x1
#define BQ2515X_ICCTRL0_AUTOWAKE_2_4_SEC 0x2
#define BQ2515X_ICCTRL0_AUTOWAKE_5_SEC   0x3
#define BQ2515X_ICCTRL0_GLOBAL_INT_MASK  BIT(2)
#define BQ2515X_ICCTRL0_HW_RESET         BIT(1)
#define BQ2515X_ICCTRL0_SW_RESET         BIT(0)

#define BQ2515X_ICCTRL1_ADDR                       0x36
#define BQ2515X_ICCTRL1_MR_LPRESS_ACTION           GENMASK(7, 6)
#define BQ2515X_ICCTRL1_MR_LPRESS_ACTION_HW_RESET  0x0
#define BQ2515X_ICCTRL1_MR_LPRESS_ACTION_NONE      0x1
#define BQ2515X_ICCTRL1_MR_LPRESS_ACTION_SHIP_MODE 0x2
#define BQ2515X_ICCTRL1_ADCIN_MODE                 BIT(5)
#define BQ2515X_ICCTRL1_ADCIN_MODE_ADC             0
#define BQ2515X_ICCTRL1_ADCIN_MODE_NTC             1
#define BQ2515X_ICCTRL1_PG_MODE                    GENMASK(3, 2)
#define BQ2515X_ICCTRL1_PG_MODE_VIN_PG             0x0
#define BQ2515X_ICCTRL1_PG_MODE_MR                 0x1
#define BQ2515X_ICCTRL1_PG_MODE_GPO                0x2
#define BQ2515X_ICCTRL1_PMID_MODE                  GENMASK(1, 0)
#define BQ2515X_ICCTRL1_PMID_MODE_BAT_VIN          0x0
#define BQ2515X_ICCTRL1_PMID_MODE_BAT              0x1
#define BQ2515X_ICCTRL1_PMID_MODE_FLOAT            0x2
#define BQ2515X_ICCTRL1_PMID_MODE_PULL_DOWN        0x3

#define BQ2515X_ICCTRL2_ADDR            0x37
#define BQ25155_ICCTRL2_PMID_REG_CTRL   GENMASK(7, 5)
#define BQ2515X_ICCTRL2_GPO_PG          BIT(4)
#define BQ2515X_ICCTRL2_HWRESET_14S_WD  BIT(1)
#define BQ2515X_ICCTRL2_CHARGER_DISABLE BIT(0)

#define BQ2515X_DEVICE_ID_ADDR 0x6f
#define BQ25150_DEVICE_ID      0x20
#define BQ25155_DEVICE_ID      0x35
#define BQ25157_DEVICE_ID      0x3c

enum mfd_bq2515x_event {
	BQ2515X_EVENT_CHARGE_CV,
	BQ2515X_EVENT_CHARGE_DONE,
	BQ2515X_EVENT_IINLIM_ACTIVE,
	BQ2515X_EVENT_VDPPM_ACTIVE,
	BQ2515X_EVENT_VINDPM_ACTIVE,
	BQ2515X_EVENT_THERMREG_ACTIVE,
	BQ2515X_EVENT_VIN_PGOOD,
	BQ2515X_EVENT_VIN_OVP_FAULT,
	BQ2515X_EVENT_BAT_OCP_FAULT,
	BQ2515X_EVENT_BAT_UVLO_FAULT,
	BQ2515X_EVENT_TS_COLD,
	BQ2515X_EVENT_TS_COOL,
	BQ2515X_EVENT_TS_WARM,
	BQ2515X_EVENT_TS_HOT,
	BQ2515X_EVENT_ADC_READY,
	BQ2515X_EVENT_COMP1_ALARM,
	BQ2515X_EVENT_COMP2_ALARM,
	BQ2515X_EVENT_COMP3_ALARM,
	BQ2515X_EVENT_TS_OPEN,
	BQ2515X_EVENT_WD_FAULT,
	BQ2515X_EVENT_SAFETY_TIMER_FAULT,
	BQ2515X_EVENT_LDO_OCP_FAULT,
	BQ2515X_EVENT_MRWAKE1_TIMEOUT,
	BQ2515X_EVENT_MRWAKE2_TIMEOUT,
	BQ2515X_EVENT_MRRESET_WARN,
	BQ2515X_EVENT_MAX
};

/**
 * @brief Read multiple registers from bq2515x
 *
 * @param dev bq2515x mfd device
 * @param reg_addr Register address
 * @param data Pointer to buffer for received data
 * @param len Number of bytes to read
 * @retval 0 If successful
 * @retval -errno In case of any bus error (see i2c_write_read_dt())
 */
int mfd_bq2515x_reg_read_burst(const struct device *dev, uint8_t reg_addr, void *data, size_t len);

/**
 * @brief Read single register from bq2515x
 *
 * @param dev bq2515x mfd device
 * @param reg_addr Register address
 * @param value Pointer to buffer for register value
 * @retval 0 If successful
 * @retval -errno In case of any bus error (see i2c_write_read_dt())
 */
int mfd_bq2515x_reg_read(const struct device *dev, uint8_t reg_addr, uint8_t *value);

/**
 * @brief Write single register to bq2515x
 *
 * @param dev bq2515x mfd device
 * @param reg_addr Register address
 * @param value register value to write
 * @retval 0 If successful
 * @retval -errno In case of any bus error (see i2c_write_dt())
 */
int mfd_bq2515x_reg_write(const struct device *dev, uint8_t reg_addr, uint8_t value);

/**
 * @brief Write two registers to bq2515x
 *
 * @param dev bq2515x mfd device
 * @param reg_addr Register address
 * @param value1 value of first register to write
 * @param value2 value of second register to write
 * @retval 0 If successful
 * @retval -errno In case of any bus error (see i2c_write_dt())
 */
int mfd_bq2515x_reg_write2(const struct device *dev, uint8_t reg_addr, uint8_t value1,
			   uint8_t value2);

/**
 * @brief Update selected bits in bq2515x register
 *
 * @param dev bq2515x mfd device
 * @param reg_addr Register address
 * @param mask mask of bits to be modified
 * @param value value to write
 * @retval 0 If successful
 * @retval -errno In case of any bus error (see i2c_write_read_dt(), i2c_write_dt())
 */
int mfd_bq2515x_reg_update(const struct device *dev, uint8_t reg_addr, uint8_t mask, uint8_t value);

/**
 * @brief bq2515x software reset. All registers are reset, but power rails stay enabled.
 *
 * @param dev bq2515x mfd device
 * @retval 0 If successful
 * @retval -errno In case of any bus error (see i2c_write_dt())
 */
int mfd_bq2515x_software_reset(const struct device *dev);

/**
 * @brief Add bq2515x event callback
 *
 * @param dev bq2515x mfd device
 * @param callback callback
 * @return 0 on success, -errno on failure
 */
int mfd_bq2515x_add_callback(const struct device *dev, struct gpio_callback *callback);

/**
 * @brief Remove bq2515x event callback
 *
 * @param dev bq2515x mfd device
 * @param callback callback
 * @return 0 on success, -errno on failure
 */
int mfd_bq2515x_remove_callback(const struct device *dev, struct gpio_callback *callback);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_BQ2515X_H_ */
