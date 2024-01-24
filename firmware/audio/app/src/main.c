// SPDX-License-Identifier: GPL-3.0-or-later
#include <hal/nrf_clock.h>
#include <hal/nrf_ipc.h>
#include <inttypes.h>
#include <nrfx_dppi.h>
#include <nrfx_timer.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "adv_timer.h"
#include "freq_est.h"

LOG_MODULE_REGISTER(audio);

static const struct freq_est_params FREQ_EST_PARAMS = {
    .nominal_freq = 16000000,
    .q_theta = 0.0,
    .q_f = 256.0,
    .r = 390625.0,
    .p0 = 1e6,
};

static struct freq_est freq_est;
static struct adv_timer adv_timer;

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

void led_off_work_handler(struct k_work *work) { gpio_pin_set_dt(&led, 0); }
// Register the work handler
K_WORK_DELAYABLE_DEFINE(led_off_work, led_off_work_handler);

static struct sync_data {
    bool found;
    struct bt_le_per_adv_sync *sync;
} sync;

#define NAME_LEN 30

static bool data_cb(struct bt_data *data, void *user_data) {
    char *name = user_data;
    uint8_t len;

    switch (data->type) {
        case BT_DATA_NAME_SHORTENED:
        case BT_DATA_NAME_COMPLETE:
            len = MIN(data->data_len, NAME_LEN - 1);
            memcpy(name, data->data, len);
            name[len] = '\0';
            return false;
        default:
            return true;
    }
}

static const char *phy2str(uint8_t phy) {
    switch (phy) {
        case 0:
            return "No packets";
        case BT_GAP_LE_PHY_1M:
            return "LE 1M";
        case BT_GAP_LE_PHY_2M:
            return "LE 2M";
        case BT_GAP_LE_PHY_CODED:
            return "LE Coded";
        default:
            return "Unknown";
    }
}

static int sync_start(const struct bt_le_scan_recv_info *info) {
    if (sync.found) return -EALREADY;
    int err;

    struct bt_le_per_adv_sync_param param = {
        .sid = info->sid,
        .skip = 0,
        .timeout = 100,
    };
    bt_addr_le_copy(&param.addr, info->addr);

    err = bt_le_per_adv_sync_create(&param, &sync.sync);
    if (err < 0) {
        LOG_ERR("failed to start adv sync (err %d)\n", err);
        return err;
    }

    sync.found = true;

    return 0;
}

static void scan_recv_cb(const struct bt_le_scan_recv_info *info,
                         struct net_buf_simple *buf) {
    char le_addr[BT_ADDR_LE_STR_LEN];
    char name[NAME_LEN];

    (void)memset(name, 0, sizeof(name));

    bt_data_parse(buf, data_cb, name);

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));
    LOG_INF(
        "[DEVICE]: %s, AD evt type %u, Tx Pwr: %i, RSSI %i %s "
        "C:%u S:%u D:%u SR:%u E:%u Prim: %s, Secn: %s, "
        "Interval: 0x%04x (%u ms), SID: %u\n",
        le_addr, info->adv_type, info->tx_power, info->rssi, name,
        (info->adv_props & BT_GAP_ADV_PROP_CONNECTABLE) != 0,
        (info->adv_props & BT_GAP_ADV_PROP_SCANNABLE) != 0,
        (info->adv_props & BT_GAP_ADV_PROP_DIRECTED) != 0,
        (info->adv_props & BT_GAP_ADV_PROP_SCAN_RESPONSE) != 0,
        (info->adv_props & BT_GAP_ADV_PROP_EXT_ADV) != 0,
        phy2str(info->primary_phy), phy2str(info->secondary_phy),
        info->interval, info->interval * 5 / 4, info->sid);

    if (info->interval != 0) {
        sync_start(info);
    }
}

static struct bt_le_scan_cb scan_callbacks = {
    .recv = scan_recv_cb,
};

static void sync_cb(struct bt_le_per_adv_sync *sync,
                    struct bt_le_per_adv_sync_synced_info *info) {
    char le_addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

    LOG_INF(
        "PER_ADV_SYNC[%u]: [DEVICE]: %s synced, "
        "Interval 0x%04x (%u ms)\n",
        bt_le_per_adv_sync_get_index(sync), le_addr, info->interval,
        info->interval * 5 / 4);
}

static void term_cb(struct bt_le_per_adv_sync *sync,
                    const struct bt_le_per_adv_sync_term_info *info) {
    char le_addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

    LOG_INF("PER_ADV_SYNC[%u]: [DEVICE]: %s sync terminated\n",
            bt_le_per_adv_sync_get_index(sync), le_addr);
}

static bool data_parse_cb(struct bt_data *data, void *user_data) {
    struct adv_timer *t = (struct adv_timer *)user_data;

    if (data->type != BT_DATA_MANUFACTURER_DATA) return true;
    if (data->data_len < sizeof(struct zeus_adv_data)) return true;

    uint32_t local_time;
    uint32_t central_time;
    struct zeus_adv_data *adv_data = (struct zeus_adv_data *)data->data;

    if (adv_timer_recv(t, adv_data, &local_time, &central_time)) {
        printk("sync,%" PRIu32 ",%" PRIu32 "\n", local_time, central_time);
        freq_est_update(&freq_est, local_time, central_time);
    }

    return false;
}

static void recv_cb(struct bt_le_per_adv_sync *sync,
                    const struct bt_le_per_adv_sync_recv_info *info,
                    struct net_buf_simple *buf) {
    bt_data_parse(buf, data_parse_cb, &adv_timer);

    gpio_pin_set_dt(&led, 1);
    k_work_schedule(&led_off_work, K_MSEC(50));
}

static struct bt_le_per_adv_sync_cb sync_callbacks = {
    .synced = sync_cb,
    .term = term_cb,
    .recv = recv_cb,
};

/// Initialize periodic advertisement scanning
static int sync_scan_init(void) {
    bt_le_scan_cb_register(&scan_callbacks);
    bt_le_per_adv_sync_cb_register(&sync_callbacks);

    int err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, NULL);
    if (err) {
        LOG_ERR("Failed to start scan (err %d)\n", err);
        return err;
    }

    return 0;
}

struct onoff_client hf_cli;

int main(void) {
    int err;

    // Initialize the Bluetooth Subsystem
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("failed to enable Bluetooth (err %d)\n", err);
        return 0;
    }

    freq_est_init(&freq_est, &FREQ_EST_PARAMS);

    err = adv_timer_init(&adv_timer);
    if (err) {
        return 0;
    }

    err = sync_scan_init();
    if (err) {
        return 0;
    }

    LOG_INF("Booted");

    nrf_clock_hfclk_t type;
    bool running =
        nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &type);
    LOG_INF("HFCLK: running: %d, type: %d, expected: %d", running, type,
            NRF_CLOCK_HFCLK_HIGH_ACCURACY);

    return 0;
}