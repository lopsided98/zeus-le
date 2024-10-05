// SPDX-License-Identifier: GPL-3.0-or-later
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wifi_common);

#define WIFI_NODE DT_NODELABEL(nrf70)

#if DT_NODE_EXISTS(WIFI_NODE)

/// The nRF7002 consumes an abnormally large amount of power (~90mA) on power
/// up. This seems to happen because the IOVDD_CTRL pin is not pulled low in the
/// WT02C40C module. Therefore, this function sets both IOVDD_CTRL and BUCK_EN
/// low as soon as possible during boot. Toggling BUCK_EN high for a short
/// period also seems to work, but less reliably.
int wifi_power_off(void) {
    const struct gpio_dt_spec iovdd_ctrl_gpio =
        GPIO_DT_SPEC_GET(WIFI_NODE, iovdd_ctrl_gpios);
    const struct gpio_dt_spec bucken_gpio =
        GPIO_DT_SPEC_GET(WIFI_NODE, bucken_gpios);

    if (!device_is_ready(iovdd_ctrl_gpio.port)) {
        LOG_ERR("IOVDD GPIO %s is not ready", iovdd_ctrl_gpio.port->name);
        return -ENODEV;
    }

    if (!device_is_ready(bucken_gpio.port)) {
        LOG_ERR("BUCKEN GPIO %s is not ready", bucken_gpio.port->name);
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&iovdd_ctrl_gpio, GPIO_OUTPUT_LOW);
    if (ret) {
        LOG_ERR("IOVDD GPIO configuration failed (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&bucken_gpio, GPIO_OUTPUT_LOW);
    if (ret) {
        LOG_ERR("BUCKEN GPIO configuration failed (err %d)", ret);
        return ret;
    }

    return 0;
}

#endif
