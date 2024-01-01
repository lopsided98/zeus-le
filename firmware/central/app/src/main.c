// SPDX-License-Identifier: GPL-3.0-or-later
#include <inttypes.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sync.h"

LOG_MODULE_REGISTER(central);

struct ipc_ept packet_timer_ept;

void packet_timer_ipc_recv(const void *data, size_t len, void *priv) {
    LOG_INF("pkt");
}

static const struct ipc_ept_cfg packet_timer_ept_cfg = {
    .name = "packet_timer",
    .cb = {
        .received = packet_timer_ipc_recv,
    }};

int packet_timer_init(void) {
    int err;
    const struct device *ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));

    err = ipc_service_open_instance(ipc);
    if (err < 0 && err != -EALREADY) {
        LOG_ERR("failed to initialize IPC (err %d)\n", err);
        return err;
    }
    err = ipc_service_register_endpoint(ipc, &packet_timer_ept,
                                        &packet_timer_ept_cfg);
    if (err < 0) {
        LOG_ERR("failed to register IPC endpoint (err %d)\n", err);
        return err;
    }

    return 0;
}

int connect_adv_init(void) {
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY |
                   BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
        .interval_min = BT_GAP_ADV_SLOW_INT_MIN,
        .interval_max = BT_GAP_ADV_SLOW_INT_MAX,
        .peer = NULL,
    };

    struct bt_le_ext_adv *adv;
    int err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
    if (err) {
        LOG_ERR("Failed to create advertising set (err %d)\n", err);
        return err;
    }

    err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        LOG_ERR("Failed to start extended advertising (err %d)\n", err);
        return err;
    }

    return 0;
}

/// Initialize periodic advertisements for syncing
int sync_adv_init(void) {
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 1,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY,
        .interval_min = BT_GAP_ADV_SLOW_INT_MIN,
        .interval_max = BT_GAP_ADV_SLOW_INT_MAX,
        .peer = NULL,
    };

    struct bt_le_ext_adv *adv;
    int err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
    if (err) {
        LOG_ERR("Failed to create sync advertising set (err %d)\n", err);
        return err;
    }

    // Set periodic advertising parameters
    err = bt_le_per_adv_set_param(
        adv, BT_LE_PER_ADV_PARAM(BT_GAP_PER_ADV_SLOW_INT_MIN,
                                 BT_GAP_PER_ADV_SLOW_INT_MAX,
                                 BT_LE_PER_ADV_OPT_NONE));
    if (err) {
        LOG_ERR("Failed to set periodic sync advertising parameters (err %d)\n",
                err);
        return err;
    }

    // Enable Periodic Advertising
    err = bt_le_per_adv_start(adv);
    if (err) {
        LOG_ERR("Failed to enable periodic sync advertising (err %d)\n", err);
        return err;
    }

    err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        LOG_ERR("Failed to start sync advertising (err %d)\n", err);
        return err;
    }

    return 0;
}

int main(void) {
    int err;

    // Initialize the Bluetooth Subsystem
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("failed to enable Bluetooth (err %d)\n", err);
        return 0;
    }

    err = packet_timer_init();
    if (err) {
        return 0;
    }

    err = connect_adv_init();
    if (err) {
        return 0;
    }

    err = sync_adv_init();
    if (err) {
        return 0;
    }

    LOG_INF("Booted");

    return 0;
}