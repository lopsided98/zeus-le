// SPDX-License-Identifier: GPL-3.0-or-later
#include <nrfx_clock.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

#include "sync.h"
#include "zeus/led.h"
#include "zeus/power.h"
#include "zeus/protocol.h"
#include "zeus/usb.h"
#include "zeus/wifi.h"

LOG_MODULE_REGISTER(central, LOG_LEVEL_DBG);

static K_MUTEX_DEFINE(central_mutex);

void button_release_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(central_button_release_work,
                               button_release_work_handler);

enum central_state {
    CENTRAL_STATE_IDLE,
    CENTRAL_STATE_PAIRING,
    CENTRAL_STATE_RECORDING,
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_SOME, ZEUS_BT_UUID_VAL),
};

static const struct central_config {
    struct k_mutex *mutex;
    struct gpio_dt_spec button_gpio;
    struct k_work_delayable *button_release_work;
} central_config = {
    .mutex = &central_mutex,
    .button_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(button), gpios),
    .button_release_work = &central_button_release_work,
};

static struct central_data {
    enum central_state state;
    struct bt_le_adv_param adv_param;
    struct bt_le_ext_adv *adv;
    struct gpio_callback button_release_cb;
} central_data = {
    .state = CENTRAL_STATE_IDLE,
    .adv_param =
        {
            .id = BT_ID_DEFAULT,
            .sid = 0,
            .secondary_max_skip = 0,
            .options = BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY |
                       BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME |
                       BT_LE_ADV_OPT_FILTER_CONN |
                       BT_LE_ADV_OPT_FILTER_SCAN_REQ,
            .interval_min = BT_GAP_ADV_SLOW_INT_MIN,
            .interval_max = BT_GAP_ADV_SLOW_INT_MAX,
            .peer = NULL,
        },
};

static void add_bonded_addr_to_filter_list(const struct bt_bond_info *info,
                                           void *data) {
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_le_filter_accept_list_add(&info->addr);
    bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));
    LOG_DBG("added %s to advertising accept filter list\n", addr_str);
}

static int connect_adv_init(void) {
    struct central_data *c = &central_data;

    bt_foreach_bond(BT_ID_DEFAULT, add_bonded_addr_to_filter_list, NULL);

    int ret = bt_le_ext_adv_create(&c->adv_param, NULL, &c->adv);
    if (ret) {
        LOG_ERR("failed to create advertising set (err %d)", ret);
        return ret;
    }

    ret = bt_le_ext_adv_set_data(c->adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (ret) {
        LOG_ERR("failed to set advertising data (err %d)", ret);
        return ret;
    }

    ret = bt_le_ext_adv_start(c->adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (ret) {
        LOG_ERR("failed to start extended advertising (err %d)", ret);
        return ret;
    }

    return 0;
}

static int connect_adv_set_pairing(bool pairing) {
    struct central_data *c = &central_data;

    // Allow connections from any device while pairing
    if (pairing) {
        c->adv_param.options &=
            ~(BT_LE_ADV_OPT_FILTER_CONN | BT_LE_ADV_OPT_FILTER_SCAN_REQ);
    } else {
        c->adv_param.options |=
            BT_LE_ADV_OPT_FILTER_CONN | BT_LE_ADV_OPT_FILTER_SCAN_REQ;
    }

    return bt_le_ext_adv_update_param(c->adv, &c->adv_param);
}

static int central_pair(void) {
    const struct central_config *config = &central_config;
    struct central_data *data = &central_data;
    int ret;

    k_mutex_lock(config->mutex, K_FOREVER);

    if (data->state != CENTRAL_STATE_IDLE) {
        ret = -EBUSY;
        goto unlock;
    }

    ret = bt_le_ext_adv_stop(data->adv);
    if (ret) {
        LOG_INF("failed to stop advertising (err %d)", ret);
        goto unlock;
    }

    ret = connect_adv_set_pairing(true);
    if (ret) {
        LOG_INF("failed to enable pairing (err %d)", ret);
        goto unlock;
    }

    ret = bt_le_ext_adv_start(data->adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (ret) {
        LOG_INF("failed to start advertising (err %d)", ret);
        goto unlock;
    }

    data->state = CENTRAL_STATE_PAIRING;
    LOG_INF("pairing started...");

unlock:
    k_mutex_unlock(config->mutex);
    return ret;
}

static int central_start(void) {
    const struct central_config *config = &central_config;
    struct central_data *data = &central_data;
    int ret;

    k_mutex_lock(config->mutex, K_FOREVER);

    if (data->state == CENTRAL_STATE_PAIRING) {
        ret = -EBUSY;
        goto unlock;
    }

    ret = sync_cmd_start();
    if (ret) goto unlock;

    data->state = CENTRAL_STATE_RECORDING;

unlock:
    k_mutex_unlock(config->mutex);
    return ret;
}

static int central_stop(void) {
    const struct central_config *config = &central_config;
    struct central_data *data = &central_data;
    int ret;

    k_mutex_lock(config->mutex, K_FOREVER);

    if (data->state == CENTRAL_STATE_PAIRING) {
        ret = -EBUSY;
        goto unlock;
    }

    ret = sync_cmd_stop();
    if (ret) goto unlock;

    data->state = CENTRAL_STATE_IDLE;

unlock:
    k_mutex_unlock(config->mutex);
    return ret;
}

static int central_toggle(void) {
    const struct central_config *config = &central_config;
    struct central_data *data = &central_data;
    int ret;

    k_mutex_lock(config->mutex, K_FOREVER);

    switch (data->state) {
        case CENTRAL_STATE_PAIRING:
            ret = -EBUSY;
            goto unlock;
        case CENTRAL_STATE_IDLE:
            ret = sync_cmd_start();
            if (ret) goto unlock;
            data->state = CENTRAL_STATE_RECORDING;
            break;
        case CENTRAL_STATE_RECORDING:
            ret = sync_cmd_stop();
            if (ret) goto unlock;
            data->state = CENTRAL_STATE_IDLE;
            break;
    }

unlock:
    k_mutex_unlock(config->mutex);
    return ret;
}

static int cmd_pair(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start pairing command");
    return central_pair();
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start recording command");
    int ret = central_start();
    if (ret) {
        shell_error(sh, "failed to send start command (err %d)", ret);
    }
    return ret;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "stop recording command");
    int ret = central_stop();
    if (ret) {
        shell_error(sh, "failed to send stop command (err %d)", ret);
    }
    return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_zeus, SHELL_CMD(pair, NULL, "Pair new audio node", cmd_pair),
    SHELL_CMD(start, NULL, "Start recording", cmd_start),
    SHELL_CMD(stop, NULL, "Stop recording", cmd_stop), SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(zeus, &sub_zeus, "Zeus commands", NULL);

void button_release_work_handler(struct k_work *work) { central_toggle(); }

void button_release_handler(const struct device *port, struct gpio_callback *cb,
                            gpio_port_pins_t pins) {
    const struct central_config *config = &central_config;
    // Delay for debouncing
    k_work_reschedule(config->button_release_work, K_MSEC(50));
}

int button_init(void) {
    const struct central_config *config = &central_config;
    struct central_data *data = &central_data;
    int ret;

    gpio_init_callback(&data->button_release_cb, button_release_handler,
                       BIT(config->button_gpio.pin));

    ret = gpio_add_callback(config->button_gpio.port, &data->button_release_cb);
    if (ret) {
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&config->button_gpio,
                                          GPIO_INT_EDGE_TO_INACTIVE);
    if (ret) {
        return ret;
    }

    return 0;
}

WIFI_POWER_OFF_REGISTER();

int cpu_clock_128_mhz(void) {
    nrfx_err_t err =
        nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
    if (err != NRFX_SUCCESS) {
        LOG_WRN("Failed to set CPU to 128 MHz: %d", err - NRFX_ERROR_BASE_NUM);
        return -1;
    }
    return 0;
}

int main(void) {
    int ret;

    ret = power_init();
    if (ret) {
        LOG_ERR("power init failed (err %d)", ret);
    }

    cpu_clock_128_mhz();

    ret = led_boot();
    if (ret) {
        LOG_ERR("failed to set LED (err %d)", ret);
    }

    // Initialize the Bluetooth Subsystem
    ret = bt_enable(NULL);
    if (ret) {
        LOG_ERR("failed to enable Bluetooth (err %d)", ret);
        return 0;
    }

    ret = settings_load();
    if (ret) {
        LOG_WRN("failed to load settings (err %d)", ret);
        // No return, settings failure is not fatal
    }

    usb_init();

    ret = connect_adv_init();
    if (ret) {
        return 0;
    }

    ret = sync_init();
    if (ret) {
        return 0;
    }

    button_init();

    LOG_INF("Booted");

    return 0;
}