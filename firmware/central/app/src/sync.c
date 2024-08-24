// SPDX-License-Identifier: GPL-3.0-or-later
#include "zeus/sync.h"

#include <inttypes.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zeus/protocol.h"

LOG_MODULE_REGISTER(sync);

/// Delay from start command to start of recording. Must be long enough for
/// audio nodes to reliably receive command.
#define SYNC_START_DELAY (2 * ZEUS_TIME_NOMINAL_FREQ)

static void sync_adv_update_handler(struct k_work *work);

K_WORK_DEFINE(sync_update_work, sync_adv_update_handler);

K_MSGQ_DEFINE(sync_cmd_queue, sizeof(struct zeus_adv_cmd), 2, 1);

static struct sync {
    // Resources
    struct ipc_ept ept;
    struct bt_le_ext_adv *adv;
    struct zeus_adv_data adv_data;
    struct k_work *update_work;
    struct k_msgq *cmd_queue;

    // State
    bool first_seq;
    uint8_t prev_seq;
    /// Timestamp of the last advertising packet sent
    atomic_t last_pkt_time;
    /// Current command sequence number
    uint16_t cmd_seq;
} sync = {
    .update_work = &sync_update_work,
    .cmd_queue = &sync_cmd_queue,

    .first_seq = true,
};

#define LED_ENABLED DT_NODE_EXISTS(DT_NODELABEL(led0))

#if LED_ENABLED
static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

static void led_off_work_handler(struct k_work *work) {
    gpio_pin_set_dt(&led, 0);
}
K_WORK_DELAYABLE_DEFINE(led_off_work, led_off_work_handler);
#endif

static int sync_adv_update_data(void) {
    struct sync *t = &sync;

    size_t body_len;
    switch (t->adv_data.cmd.hdr.id) {
        case ZEUS_ADV_CMD_START:
            body_len = sizeof(struct zeus_adv_cmd_header) +
                       sizeof(struct zeus_adv_cmd_start);
            break;
        case ZEUS_ADV_CMD_STOP:
            body_len = sizeof(struct zeus_adv_cmd_header);
            break;
        default:
        case ZEUS_ADV_CMD_NONE:
            t->adv_data.cmd = (struct zeus_adv_cmd){
                .hdr.id = ZEUS_ADV_CMD_NONE,
            };
            body_len = 0;
            break;
    }
    size_t len = sizeof(struct zeus_adv_header) + body_len;
    __ASSERT(len <= sizeof(struct zeus_adv_data),
             "Advertising data length calculation error");

    struct bt_data ad[] = {
        BT_DATA(BT_DATA_MANUFACTURER_DATA, &t->adv_data, len),
    };

    return bt_le_per_adv_set_data(t->adv, ad, ARRAY_SIZE(ad));
}

static void sync_adv_update_handler(struct k_work *work) {
    int err = sync_adv_update_data();
    if (err < 0) {
        LOG_ERR("failed to set advertising data (err %d)", err);
        return;
    }
}

static void sync_ipc_recv(const void *data, size_t len, void *priv) {
    struct sync *t = priv;
    int err;
    const struct zeus_sync_msg *msg = data;

    if (msg->seq != (uint8_t)(t->prev_seq + 1) && !t->first_seq) {
        LOG_WRN("seq mismatch: %" PRIu8 " != %" PRIu8, msg->seq,
                t->prev_seq + 1);
    }
    t->prev_seq = msg->seq;
    t->first_seq = false;
    atomic_set(&t->last_pkt_time, msg->time);

    // LOG_INF("pkt");

#if LED_ENABLED
    gpio_pin_set_dt(&led, 1);
    k_work_schedule(&led_off_work, K_MSEC(50));
#endif

    t->adv_data = (struct zeus_adv_data){
        .hdr =
            {
                .seq = msg->seq,
                .time = msg->time,
            },
    };

    err = k_msgq_get(t->cmd_queue, &t->adv_data.cmd, K_NO_WAIT);
    if (err) {
        // Queue empty, no command
        t->adv_data.cmd = (struct zeus_adv_cmd){
            .hdr.id = ZEUS_ADV_CMD_NONE,
        };
    } else {
        t->adv_data.cmd.hdr.seq = t->cmd_seq++;
    }

    k_work_submit(t->update_work);
}

static const struct ipc_ept_cfg sync_ept_cfg = {
    .name = "packet_timer",
    .cb.received = sync_ipc_recv,
    .priv = &sync,
};

/// Initialize periodic advertisements for syncing
static int sync_adv_init(void) {
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 1,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY,
        .interval_min = BT_GAP_ADV_SLOW_INT_MIN,
        .interval_max = BT_GAP_ADV_SLOW_INT_MAX,
        .peer = NULL,
    };

    int ret = bt_le_ext_adv_create(&adv_param, NULL, &sync.adv);
    if (ret) {
        LOG_ERR("failed to create sync advertising set (err %d)", ret);
        return ret;
    }

    // Set periodic advertising parameters
    ret = bt_le_per_adv_set_param(
        sync.adv, BT_LE_PER_ADV_PARAM(BT_GAP_PER_ADV_FAST_INT_MIN_2,
                                      BT_GAP_PER_ADV_FAST_INT_MAX_2,
                                      BT_LE_PER_ADV_OPT_NONE));
    if (ret) {
        LOG_ERR("failed to set periodic sync advertising parameters (err %d)",
                ret);
        return ret;
    }

    // Enable Periodic Advertising
    ret = bt_le_per_adv_start(sync.adv);
    if (ret) {
        LOG_ERR("failed to enable periodic sync advertising (err %d)\n", ret);
        return ret;
    }

    ret = bt_le_ext_adv_start(sync.adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (ret) {
        LOG_ERR("failed to start sync advertising (err %d)\n", ret);
        return ret;
    }

    return 0;
}

int sync_init(void) {
    struct sync *s = &sync;
    int ret;

#if LED_ENABLED
    gpio_pin_configure_dt(&led, GPIO_OUTPUT);
#endif

    const struct device *ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));

    ret = ipc_service_open_instance(ipc);
    if (ret < 0 && ret != -EALREADY) {
        LOG_ERR("failed to initialize IPC (err %d)", ret);
        return ret;
    }
    ret = ipc_service_register_endpoint(ipc, &s->ept, &sync_ept_cfg);
    if (ret < 0) {
        LOG_ERR("failed to register IPC endpoint (err %d)", ret);
        return ret;
    }

    ret = sync_adv_init();
    if (ret) return ret;

    return 0;
}

int sync_cmd_start(void) {
    struct sync *s = &sync;

    uint32_t start_time = atomic_get(&s->last_pkt_time) + SYNC_START_DELAY;

    return k_msgq_put(s->cmd_queue,
                      &(struct zeus_adv_cmd){
                          .hdr.id = ZEUS_ADV_CMD_START,
                          .start.time = start_time,
                      },
                      K_NO_WAIT);
}

int sync_cmd_stop(void) {
    struct sync *s = &sync;

    return k_msgq_put(s->cmd_queue,
                      &(struct zeus_adv_cmd){
                          .hdr.id = ZEUS_ADV_CMD_STOP,
                      },
                      K_NO_WAIT);
}