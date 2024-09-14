// SPDX-License-Identifier: GPL-3.0-or-later
#include <nrfx_clock.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

#include "sync.h"
#include "zeus/protocol.h"
#include "zeus/usb.h"
#include "zeus/wifi.h"

LOG_MODULE_REGISTER(central, LOG_LEVEL_DBG);

enum central_state {
    STATE_IDLE,
    STATE_PAIRING,
    STATE_RECORDING,
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_SOME, ZEUS_BT_UUID_VAL),
};

static struct central {
    enum central_state state;
    struct bt_le_adv_param adv_param;
    struct bt_le_ext_adv *adv;
} central = {
    .state = STATE_IDLE,
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
    struct central *c = &central;

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
    struct central *c = &central;

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
    struct central *c = &central;
    int ret;

    if (c->state != STATE_IDLE) {
        return -EALREADY;
    }

    ret = bt_le_ext_adv_stop(c->adv);
    if (ret) {
        LOG_INF("failed to stop advertising (err %d)", ret);
        return ret;
    }

    ret = connect_adv_set_pairing(true);
    if (ret) {
        LOG_INF("failed to enable pairing (err %d)", ret);
        return ret;
    }

    ret = bt_le_ext_adv_start(c->adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (ret) {
        LOG_INF("failed to start advertising (err %d)", ret);
        return ret;
    }

    c->state = STATE_PAIRING;
    LOG_INF("pairing started...");
    return 0;
}

static int cmd_pair(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start pairing command");
    return central_pair();
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start recording command");
    int ret = sync_cmd_start();
    if (ret) {
        shell_fprintf(sh, SHELL_ERROR, "failed to send start command (err %d)",
                      ret);
    }
    return ret;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "stop recording command");
    int ret = sync_cmd_stop();
    if (ret) {
        shell_fprintf(sh, SHELL_ERROR, "failed to send stop command (err %d)",
                      ret);
    }
    return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_zeus, SHELL_CMD(pair, NULL, "Pair new audio node", cmd_pair),
    SHELL_CMD(start, NULL, "Start recording", cmd_start),
    SHELL_CMD(stop, NULL, "Stop recording", cmd_stop), SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(zeus, &sub_zeus, "Zeus commands", NULL);

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
    cpu_clock_128_mhz();

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

    LOG_INF("Booted");

    return 0;
}