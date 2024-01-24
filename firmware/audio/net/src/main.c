// SPDX-License-Identifier: GPL-3.0-or-later
#include <hal/nrf_ipc.h>
#include <inttypes.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_clock.h>

#include "hal/nrf_ipc.h"
#include "hci_ipc.h"
#include "zeus/sync.h"

// Zephyr internal headers, order is important
// clang-format off
#include <bluetooth/controller/ll_sw/pdu_df.h>
#include <bluetooth/controller/ll_sw/nordic/lll/pdu_vendor.h>
#include <bluetooth/controller/ll_sw/pdu.h>
// clang-format on

LOG_MODULE_REGISTER(audio_net);

#define PACKET_TIMER_EGU_IDX 0

struct packet_timer {
} packet_timer;

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

    nrf_clock_hfclk_t type;
    bool running = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &type);
    LOG_INF("net HFCLK: running: %d, type: %d", running, type);

    return 0;
}