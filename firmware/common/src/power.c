// SPDX-License-Identifier: GPL-3.0-or-later
#include "zeus/power.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/poweroff.h>
#include <zeus/drivers/mfd/bq2515x.h>

#include "zeus/led.h"

LOG_MODULE_REGISTER(power);

enum power_event {
    POWER_EVENT_BOOTED = BIT(0),
    POWER_EVENT_MR_WAKE2 = BIT(1),
    POWER_EVENT_CHARGE = BIT(2),
};

static K_EVENT_DEFINE(power_event);

static const struct power_config {
    const struct device *mfd;
    const struct device *charger;
    const struct device *charger_regulators;
    const struct gpio_dt_spec button_gpio;
    struct k_event *event;
} power_config = {
    .mfd = DEVICE_DT_GET(DT_NODELABEL(charger_mfd)),
    .charger = DEVICE_DT_GET(DT_NODELABEL(charger)),
    .charger_regulators = DEVICE_DT_GET(DT_NODELABEL(charger_regulators)),
    .button_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(button), gpios),
    .event = &power_event,
};

static struct power_data {
    struct gpio_callback mr_wake2_cb;
} power_data;

/// Immediately shut off power without shutting down first
static void power_off(void) {
    const struct power_config *config = &power_config;
    int err;

    led_shutdown();

    // Enable logging panic mode to flush logs before shutting down
    LOG_PANIC();
    // Shell backend doesn't actually flush synchronously (bug), so wait a
    // little bit
    k_sleep(K_MSEC(100));

    err = regulator_parent_ship_mode(config->charger_regulators);
    if (err) {
        LOG_WRN("failed to enter ship mode (err %d)", err);
    }
    // If VIN is connected, ship mode won't activate, so shutdown the processor
    // instead
    sys_poweroff();
}

/// Perform an orderly shutdown and power off
static void power_shutdown(void) {
    LOG_INF("shutting down...");

    TYPE_SECTION_FOREACH(power_shutdown_hook, zeus_power_shutdown_hooks, hook) {
        (*hook)();
    }
    power_off();
}

/// Update the LED state according to the
static int power_update_charge_status(void) {
    const struct power_config *config = &power_config;
    int ret;

    union charger_propval val;
    ret = charger_get_prop(config->charger, CHARGER_PROP_STATUS, &val);
    if (ret) return ret;

    switch (val.status) {
        case CHARGER_STATUS_CHARGING:
            led_battery_charging();
            break;
        case CHARGER_STATUS_FULL:
            led_battery_full();
            break;
        case CHARGER_STATUS_DISCHARGING:
        default:
            led_battery_discharging();
            break;
    }

    return 0;
}

static void power_mr_wake2_handler(const struct device *dev,
                                   struct gpio_callback *cb,
                                   gpio_port_pins_t events) {
    const struct power_config *config = &power_config;

    if (events & BIT(BQ2515X_EVENT_MRWAKE2_TIMEOUT)) {
        k_event_post(config->event, POWER_EVENT_MR_WAKE2);
        if (k_event_test(config->event, POWER_EVENT_BOOTED)) {
            // Not during initial boot, so this is a shutdown request
            power_shutdown();
        }
    }

    if (events &
        (BIT(BQ2515X_EVENT_CHARGE_DONE) | BIT(BQ2515X_EVENT_VIN_PGOOD))) {
        power_update_charge_status();
        k_event_post(config->event, POWER_EVENT_CHARGE);
    }
}

extern void zeus_shell_hack(void);

int power_init(void) {
    const struct power_config *config = &power_config;
    struct power_data *data = &power_data;
    int ret;

    // Stupid hack to make iterable sections work
    zeus_shell_hack();

    if (!device_is_ready(config->charger)) {
        LOG_ERR("battery charger not ready");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&config->button_gpio)) {
        LOG_ERR("button GPIO device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->button_gpio, GPIO_INPUT);
    if (ret < 0) return ret;

    power_update_charge_status();

    gpio_init_callback(&data->mr_wake2_cb, power_mr_wake2_handler,
                       BIT(BQ2515X_EVENT_MRWAKE2_TIMEOUT) |
                           BIT(BQ2515X_EVENT_CHARGE_DONE) |
                           BIT(BQ2515X_EVENT_VIN_PGOOD));
    ret = mfd_bq2515x_add_callback(config->mfd, &data->mr_wake2_cb);
    if (ret) return ret;

    uint32_t reset;
    ret = hwinfo_get_reset_cause(&reset);
    if (ret) {
        LOG_ERR("failed to get reset cause (err %d)", ret);
        return ret;
    }
    hwinfo_clear_reset_cause();

    LOG_DBG("reset: 0x%08" PRIx32, reset);

    bool power_on = false;
    // Continue booting if button is pressed. This allows us to distinguish
    // between wake from ship due to MR_WAKE2 or VIN.
    if (gpio_pin_get_dt(&config->button_gpio)) {
        power_on = true;
    }

    // Power on after a software reset. This makes development easier.
    if (reset & RESET_SOFTWARE) {
        power_on = true;
    }

    if (reset & RESET_LOW_POWER_WAKE) {
        // Wait for 100 ms to see if the battery charger has told us the wakeup
        // reason. If something else woke us up, the 100 ms wait doesn't matter
        // since we are just going to power off again.
        uint32_t events = k_event_wait(
            config->event, POWER_EVENT_MR_WAKE2 | POWER_EVENT_CHARGE, false,
            K_MSEC(100));
        // Also boot if we get a MR_WAKE2 event. This happens while powered off
        // (but not in ship mode) with VIN connected. Checking the button state
        // above is not enough because the user can release it before the OS has
        // booted.
        if (events & POWER_EVENT_MR_WAKE2) {
            power_on = true;
        }
    }

    if (!power_on) {
        power_off();
    }

    k_event_post(config->event, POWER_EVENT_BOOTED);
    return 0;
}
