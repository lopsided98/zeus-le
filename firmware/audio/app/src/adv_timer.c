// SPDX-License-Identifier: GPL-3.0-or-later
#include "adv_timer.h"

#include <hal/nrf_ipc.h>
#include <nrfx_dppi.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/logging/log.h>

#include "zeus/protocol.h"
#include "zeus/sync.h"

LOG_MODULE_REGISTER(adv_timer);

#define ADV_TIMER_INDEX 2
#define ADV_TIMER_CAPTURE_CHANNEL 0

int adv_timer_init(struct adv_timer *t) {
    nrfx_err_t nerr;

    const struct device *mbox = DEVICE_DT_GET(DT_NODELABEL(mbox));
    struct mbox_channel channel;

    mbox_init_channel(&channel, mbox, ZEUS_PACKET_END_MBOX_CHANNEL);

    t->timer = (nrfx_timer_t)NRFX_TIMER_INSTANCE(ADV_TIMER_INDEX);

    // Setup 32-bit 16 MHz timer to capture on radio end event
    nerr = nrfx_timer_init(&t->timer,
                           &(nrfx_timer_config_t){
                               .frequency = ADV_TIMER_FREQ,
                               .mode = NRF_TIMER_MODE_TIMER,
                               .bit_width = NRF_TIMER_BIT_WIDTH_32,
                           },
                           NULL);
    NRFX_ASSERT(NRFX_SUCCESS == nerr);

    uint8_t dppi;
    nerr = nrfx_dppi_channel_alloc(&dppi);
    if (NRFX_SUCCESS != nerr) {
        LOG_ERR("failed to allocate DPPI channel (err %d)\n", nerr);
        return -ENOMEM;
    }

    // Configure MBOX IPC channel receive to trigger timer capture
    nrf_ipc_publish_set(
        NRF_IPC, nrf_ipc_receive_event_get(ZEUS_PACKET_END_MBOX_CHANNEL), dppi);
    nrf_timer_subscribe_set(
        t->timer.p_reg, nrf_timer_capture_task_get(ADV_TIMER_CAPTURE_CHANNEL),
        dppi);
    nrfx_dppi_channel_enable(dppi);

    // Keep HFCLK enabled and using HFXO all the time. This is required because
    // we need an accurate clock to run the timer.
    struct onoff_manager *mgr =
        z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    sys_notify_init_spinwait(&t->hf_cli.notify);
    onoff_request(mgr, &t->hf_cli);

    // Start the timer
    nrfx_timer_enable(&t->timer);

    return 0;
}

bool adv_timer_recv(struct adv_timer *t, const struct zeus_adv_data *data,
                    uint32_t *local_time, uint32_t *central_time) {
    bool ret = false;

    if (t->last_time_valid && data->seq == t->last_time_seq) {
        *local_time = t->last_time;
        *central_time = data->time;
        ret = true;
    }

    uint32_t time =
        nrfx_timer_capture_get(&t->timer, ADV_TIMER_CAPTURE_CHANNEL);

    t->last_time_valid = true;
    t->last_time_seq = data->seq + 1;
    t->last_time = time;

    return ret;
}