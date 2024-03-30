// SPDX-License-Identifier: GPL-3.0-or-later
#include <inttypes.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zeus/protocol.h"
#include "zeus/sync.h"

LOG_MODULE_REGISTER(central);

static struct packet_timer {
    struct ipc_ept ept;
    struct bt_le_ext_adv *adv;
    struct zeus_adv_data adv_data;
    struct k_work update_work;
    uint8_t last_seq;
    bool first_seq;
} packet_timer = {
    .first_seq = true,
};

#define LED_ENABLED !IS_ENABLED(CONFIG_ARCH_POSIX)

#if LED_ENABLED
static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

void led_off_work_handler(struct k_work *work) { gpio_pin_set_dt(&led, 0); }
/* Register the work handler */
K_WORK_DELAYABLE_DEFINE(led_off_work, led_off_work_handler);
#endif

void packet_timer_update_handler(struct k_work *work) {
    struct packet_timer *t =
        CONTAINER_OF(work, struct packet_timer, update_work);

    struct bt_data ad[] = {
        BT_DATA(BT_DATA_MANUFACTURER_DATA, &t->adv_data, sizeof(t->adv_data)),
    };

    int err = bt_le_per_adv_set_data(t->adv, ad, ARRAY_SIZE(ad));
    if (err < 0) {
        LOG_ERR("Failed to set advertising data (err %d)\n", err);
        return;
    }
}

void packet_timer_ipc_recv(const void *data, size_t len, void *priv) {
    struct packet_timer *t = priv;
    const struct zeus_packet_timer_msg *msg = data;

    if (msg->seq != (uint8_t)(t->last_seq + 1) && !t->first_seq) {
        LOG_WRN("seq mismatch: %" PRIu8 " != %" PRIu8, msg->seq,
                t->last_seq + 1);
    }
    t->last_seq = msg->seq;
    t->first_seq = false;

    // LOG_INF("pkt");

#if LED_ENABLED
    gpio_pin_set_dt(&led, 1);
    k_work_schedule(&led_off_work, K_MSEC(50));
#endif

    t->adv_data = (struct zeus_adv_data){
        .seq = msg->seq,
        .time = msg->time,
    };
    k_work_submit(&t->update_work);
}

static const struct ipc_ept_cfg packet_timer_ept_cfg = {
    .name = "packet_timer",
    .cb.received = packet_timer_ipc_recv,
    .priv = &packet_timer,
};

int packet_timer_init(void) {
    int err;
    k_work_init(&packet_timer.update_work, packet_timer_update_handler);

#if LED_ENABLED
    gpio_pin_configure_dt(&led, GPIO_OUTPUT);
#endif

    const struct device *ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));

    err = ipc_service_open_instance(ipc);
    if (err < 0 && err != -EALREADY) {
        LOG_ERR("failed to initialize IPC (err %d)\n", err);
        return err;
    }
    err = ipc_service_register_endpoint(ipc, &packet_timer.ept,
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

    int err = bt_le_ext_adv_create(&adv_param, NULL, &packet_timer.adv);
    if (err) {
        LOG_ERR("Failed to create sync advertising set (err %d)\n", err);
        return err;
    }

    // Set periodic advertising parameters
    err = bt_le_per_adv_set_param(
        packet_timer.adv, BT_LE_PER_ADV_PARAM(BT_GAP_PER_ADV_FAST_INT_MIN_2,
                                              BT_GAP_PER_ADV_FAST_INT_MAX_2,
                                              BT_LE_PER_ADV_OPT_NONE));
    if (err) {
        LOG_ERR("Failed to set periodic sync advertising parameters (err %d)\n",
                err);
        return err;
    }

    // Enable Periodic Advertising
    err = bt_le_per_adv_start(packet_timer.adv);
    if (err) {
        LOG_ERR("Failed to enable periodic sync advertising (err %d)\n", err);
        return err;
    }

    err = bt_le_ext_adv_start(packet_timer.adv, BT_LE_EXT_ADV_START_DEFAULT);
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