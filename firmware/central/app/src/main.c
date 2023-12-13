// SPDX-License-Identifier: GPL-3.0-or-later
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/printk.h>

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static void bt_ready(int err) {
    if (err) {
        printk("failed to initialize Bluetooth (err %d)\n", err);
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("failed to start advertising (err %d)\n", err);
        return;
    }
}

int main(void) {
    int err;
    err = bt_enable(bt_ready);
    if (err) {
        printk("failed to enable Bluetooth (err %d)\n", err);
    }

    return 0;
}