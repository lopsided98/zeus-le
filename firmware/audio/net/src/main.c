// SPDX-License-Identifier: GPL-3.0-or-later
#include <hal/nrf_clock.h>
#include <hal/nrf_ipc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "hci_ipc.h"
#include "zeus/sync.h"

LOG_MODULE_REGISTER(audio_net);

static int packet_timer_init(void) {
    // Subscribe to radio end event through existing DPPI channel configured by
    // BLE driver
    nrf_ipc_subscribe_set(NRF_IPC,
                          nrf_ipc_send_task_get(ZEUS_PACKET_END_MBOX_CHANNEL),
                          HAL_RADIO_END_TIME_CAPTURE_PPI);

    return 0;
}

int main(void) {
    int err;

    err = packet_timer_init();
    if (err) {
        return err;
    }

    err = hci_ipc_init();
    if (err) {
        return err;
    }

    LOG_INF("Booted");

    return 0;
}