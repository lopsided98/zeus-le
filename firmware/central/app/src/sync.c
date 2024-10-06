// SPDX-License-Identifier: GPL-3.0-or-later
#include "zeus/sync.h"

#include <inttypes.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zeus/led.h"
#include "zeus/protocol.h"

LOG_MODULE_REGISTER(sync);

/// Delay from start command to start of recording. Must be long enough for
/// audio nodes to reliably receive command.
#define SYNC_START_DELAY_SEC 2
/// Start delay in timer units
#define SYNC_START_DELAY (SYNC_START_DELAY_SEC * ZEUS_TIME_NOMINAL_FREQ)

static void sync_adv_update_handler(struct k_work *work);
static K_WORK_DEFINE(sync_update_work, sync_adv_update_handler);

K_MSGQ_DEFINE(sync_cmd_queue, sizeof(struct zeus_adv_cmd), 2, 1);

static const struct sync_config {
    struct k_work *update_work;
    struct k_msgq *cmd_queue;
} sync_config = {
    .update_work = &sync_update_work,
    .cmd_queue = &sync_cmd_queue,
};

static struct sync_data {
    // Resources
    struct ipc_ept ept;
    struct bt_le_ext_adv *adv;

    // State
    struct zeus_adv_data adv_data;
    bool first_seq;
    uint8_t prev_seq;
    /// Timestamp of the last advertising packet sent
    atomic_t last_pkt_time;
    /// Current command sequence number
    uint16_t cmd_seq;
} sync_data = {
    .first_seq = true,
};

static int32_t sync_time_diff(uint32_t a, uint32_t b) {
    return (int32_t)(a - b);
}

static int sync_adv_update_data(void) {
    const struct sync_config *config = &sync_config;
    struct sync_data *data = &sync_data;

    bool new_cmd = false;
    int err = k_msgq_get(config->cmd_queue, &data->adv_data.cmd, K_NO_WAIT);
    if (!err) {
        new_cmd = true;
    }

    switch (data->adv_data.cmd.id) {
        case ZEUS_ADV_CMD_START: {
            int32_t waiting_time = sync_time_diff(data->adv_data.cmd.start.time,
                                                  data->adv_data.hdr.sync.time);
            // FIXME: what if another non-recording related command arrives
            // before the waiting period expires
            if (waiting_time > 0) {
                led_record_waiting();
            } else {
                led_record_started();
            }
            // Clear out old start command once twice the start delay has
            // passed.
            if (waiting_time < -SYNC_START_DELAY) {
                data->adv_data.cmd =
                    (struct zeus_adv_cmd){.id = ZEUS_ADV_CMD_NONE};
                new_cmd = true;
            }
        } break;
        case ZEUS_ADV_CMD_STOP:
            led_record_stopped();
            break;
        default:
            break;
    }

    if (new_cmd) {
        data->adv_data.hdr.seq = ++data->cmd_seq;
    }

    size_t cmd_len;
    switch (data->adv_data.cmd.id) {
        case ZEUS_ADV_CMD_START:
            cmd_len = sizeof(struct zeus_adv_cmd_start);
            break;
        case ZEUS_ADV_CMD_STOP:
            cmd_len = 0;
            break;
        default:
        case ZEUS_ADV_CMD_NONE:
            // Make sure the cmd is NONE in the default case
            data->adv_data.cmd.id = ZEUS_ADV_CMD_NONE;
            cmd_len = 0;
            break;
    }
    size_t len = sizeof(struct zeus_adv_header) +
                 // No body is implicit ZEUS_ADV_CMD_NONE
                 (data->adv_data.cmd.id == ZEUS_ADV_CMD_NONE
                      ? 0
                      : sizeof(enum zeus_adv_cmd_id) + cmd_len);
    __ASSERT(len <= sizeof(struct zeus_adv_data),
             "Advertising data length calculation error");

    struct bt_data ad[] = {
        BT_DATA(BT_DATA_MANUFACTURER_DATA, &data->adv_data, len),
    };

    return bt_le_per_adv_set_data(data->adv, ad, ARRAY_SIZE(ad));
}

static void sync_adv_update_handler(struct k_work *work) {
    int err = sync_adv_update_data();
    if (err < 0) {
        LOG_ERR("failed to set advertising data (err %d)", err);
        return;
    }
}

static void sync_ipc_recv(const void *data, size_t len, void *priv) {
    const struct sync_config *config = &sync_config;
    struct sync_data *t = &sync_data;
    const struct zeus_sync_msg *msg = data;

    if (msg->seq != (uint8_t)(t->prev_seq + 1) && !t->first_seq) {
        LOG_WRN("seq mismatch: %" PRIu8 " != %" PRIu8, msg->seq,
                t->prev_seq + 1);
    }
    t->prev_seq = msg->seq;
    t->first_seq = false;
    atomic_set(&t->last_pkt_time, msg->time);

    // LOG_INF("pkt");

    t->adv_data.hdr.sync = (struct zeus_adv_sync){
        .seq = msg->seq,
        .time = msg->time,
    };

    k_work_submit(config->update_work);
}

static const struct ipc_ept_cfg sync_ept_cfg = {
    .name = "packet_timer",
    .cb.received = sync_ipc_recv,
    .priv = &sync_data,
};

/// Initialize periodic advertisements for syncing
static int sync_adv_init(void) {
    struct sync_data *data = &sync_data;

    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 1,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY,
        .interval_min = BT_GAP_ADV_SLOW_INT_MIN,
        .interval_max = BT_GAP_ADV_SLOW_INT_MAX,
        .peer = NULL,
    };

    int ret = bt_le_ext_adv_create(&adv_param, NULL, &data->adv);
    if (ret) {
        LOG_ERR("failed to create sync advertising set (err %d)", ret);
        return ret;
    }

    // Set periodic advertising parameters
    ret = bt_le_per_adv_set_param(
        data->adv, BT_LE_PER_ADV_PARAM(BT_GAP_PER_ADV_FAST_INT_MIN_2,
                                       BT_GAP_PER_ADV_FAST_INT_MAX_2,
                                       BT_LE_PER_ADV_OPT_NONE));
    if (ret) {
        LOG_ERR("failed to set periodic sync advertising parameters (err %d)",
                ret);
        return ret;
    }

    // Enable Periodic Advertising
    ret = bt_le_per_adv_start(data->adv);
    if (ret) {
        LOG_ERR("failed to enable periodic sync advertising (err %d)\n", ret);
        return ret;
    }

    ret = bt_le_ext_adv_start(data->adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (ret) {
        LOG_ERR("failed to start sync advertising (err %d)\n", ret);
        return ret;
    }

    return 0;
}

int sync_init(void) {
    struct sync_data *data = &sync_data;
    int ret;

    const struct device *ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));

    ret = ipc_service_open_instance(ipc);
    if (ret < 0 && ret != -EALREADY) {
        LOG_ERR("failed to initialize IPC (err %d)", ret);
        return ret;
    }
    ret = ipc_service_register_endpoint(ipc, &data->ept, &sync_ept_cfg);
    if (ret < 0) {
        LOG_ERR("failed to register IPC endpoint (err %d)", ret);
        return ret;
    }

    ret = sync_adv_init();
    if (ret) return ret;

    return 0;
}

int sync_cmd_start(void) {
    const struct sync_config *config = &sync_config;
    struct sync_data *data = &sync_data;

    uint32_t start_time = atomic_get(&data->last_pkt_time) + SYNC_START_DELAY;

    return k_msgq_put(config->cmd_queue,
                      &(struct zeus_adv_cmd){
                          .id = ZEUS_ADV_CMD_START,
                          .start.time = start_time,
                      },
                      K_NO_WAIT);
}

int sync_cmd_stop(void) {
    const struct sync_config *config = &sync_config;

    return k_msgq_put(config->cmd_queue,
                      &(struct zeus_adv_cmd){
                          .id = ZEUS_ADV_CMD_STOP,
                      },
                      K_NO_WAIT);
}