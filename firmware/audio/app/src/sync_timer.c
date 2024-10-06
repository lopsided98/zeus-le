// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync_timer.h"

#include <hal/nrf_i2s.h>
#include <hal/nrf_ipc.h>
#include <nrfx_dppi.h>
#include <nrfx_timer.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/onoff.h>

#include "zeus/protocol.h"
#include "zeus/sync.h"

LOG_MODULE_REGISTER(sync_timer);

#define SYNC_TIMER_INDEX 2

enum {
    SYNC_TIMER_CAPTURE_CHANNEL_ADV,
    SYNC_TIMER_CAPTURE_CHANNEL_I2S,
    SYNC_TIMER_CAPTURE_CHANNEL_USB_SOF,
    SYNC_TIMER_CAPTURE_CHANNEL_MANUAL
};

static struct sync_timer {
    // Resources
    nrfx_timer_t timer;
    struct onoff_client hf_cli;
    /// DPPI channel for I2S buffer timer capture
    uint8_t i2s_dppi;
    /// DPPI channel for USB SOF timer capture
    uint8_t usb_dppi;

    struct freq_est freq_est;

    // State
    bool init;
    /// True if a previous advertisment has been received
    bool last_adv_valid;
    /// Sequence number of the advertisment whose time is captured in last_time.
    uint8_t last_adv_seq;
    /// Local timestamp of the last received advertisment
    uint32_t last_adv_time;
} sync_timer = {
    .timer = NRFX_TIMER_INSTANCE(SYNC_TIMER_INDEX),
};

static const struct freq_est_config FREQ_EST_CONFIG = {
    .nominal_freq = ZEUS_TIME_NOMINAL_FREQ,
    .q_theta = 0.0,
    .q_f = 256.0,
    .r = 390625.0,
    .p0 = 1e6,
    .outlier_threshold = 20.0f,
    .outlier_resync_count = 5,
};

int sync_timer_init(void) {
    struct sync_timer *t = &sync_timer;
    if (t->init) return -EALREADY;

    nrfx_err_t err;

    freq_est_init(&t->freq_est, &FREQ_EST_CONFIG);

    // Setup 32-bit 16 MHz timer to capture on radio end event and I2S sample
    err = nrfx_timer_init(&t->timer,
                          &(nrfx_timer_config_t){
                              .frequency = ZEUS_TIME_NOMINAL_FREQ,
                              .mode = NRF_TIMER_MODE_TIMER,
                              .bit_width = NRF_TIMER_BIT_WIDTH_32,
                          },
                          NULL);
    NRFX_ASSERT(NRFX_SUCCESS == err);

    uint8_t adv_dppi;
    err = nrfx_dppi_channel_alloc(&adv_dppi);
    if (NRFX_SUCCESS != err) {
        LOG_ERR("failed to allocate adv DPPI channel (err %d)\n", err);
        return -ENOMEM;
    }

    // Configure MBOX IPC channel to trigger timer capture
    nrf_ipc_publish_set(NRF_IPC,
                        nrf_ipc_receive_event_get(ZEUS_PACKET_END_MBOX_CHANNEL),
                        adv_dppi);
    nrf_timer_subscribe_set(
        t->timer.p_reg,
        nrf_timer_capture_task_get(SYNC_TIMER_CAPTURE_CHANNEL_ADV), adv_dppi);
    nrfx_dppi_channel_enable(adv_dppi);

    err = nrfx_dppi_channel_alloc(&t->i2s_dppi);
    if (NRFX_SUCCESS != err) {
        LOG_ERR("failed to allocate I2S DPPI channel (err %d)\n", err);
        return -ENOMEM;
    }

    // Configure timer capture after each I2S buffer
    nrf_timer_subscribe_set(
        t->timer.p_reg,
        nrf_timer_capture_task_get(SYNC_TIMER_CAPTURE_CHANNEL_I2S),
        t->i2s_dppi);

    err = nrfx_dppi_channel_alloc(&t->usb_dppi);
    if (NRFX_SUCCESS != err) {
        LOG_ERR("failed to allocate USB DPPI channel (err %d)\n", err);
        return -ENOMEM;
    }

    // Configure timer capture on each USB SOF
    nrf_timer_subscribe_set(
        t->timer.p_reg,
        nrf_timer_capture_task_get(SYNC_TIMER_CAPTURE_CHANNEL_USB_SOF),
        t->usb_dppi);

    // Keep HFCLK enabled and using HFXO all the time. This is required because
    // we need an accurate clock to run the timer.
    struct onoff_manager *mgr =
        z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    sys_notify_init_spinwait(&t->hf_cli.notify);
    onoff_request(mgr, &t->hf_cli);

    // Start the timer
    nrfx_timer_enable(&t->timer);

    t->init = true;
    return 0;
}

void sync_timer_recv_adv(const struct zeus_adv_sync *sync) {
    struct sync_timer *t = &sync_timer;
    if (!t->init) return;

    if (t->last_adv_valid && sync->seq == t->last_adv_seq) {
        // printk("sync,%" PRIu32 ",%" PRIu32 "\n", t->last_adv_time,
        // hdr->time);
        freq_est_update(&t->freq_est, qu32_32_from_int(t->last_adv_time),
                        qu32_32_from_int(sync->time), 0);
    }

    uint32_t time =
        nrfx_timer_capture_get(&t->timer, SYNC_TIMER_CAPTURE_CHANNEL_ADV);

    t->last_adv_valid = true;
    t->last_adv_seq = sync->seq + 1;
    t->last_adv_time = time;
}

uint8_t sync_timer_get_i2s_dppi(void) {
    struct sync_timer *t = &sync_timer;
    return t->i2s_dppi;
}

uint8_t sync_timer_get_usb_sof_dppi(void) {
    struct sync_timer *t = &sync_timer;
    return t->usb_dppi;
}

uint32_t sync_timer_get_i2s_time(void) {
    struct sync_timer *t = &sync_timer;
    return nrfx_timer_capture_get(&t->timer, SYNC_TIMER_CAPTURE_CHANNEL_I2S);
}

uint32_t sync_timer_get_usb_sof_time(void) {
    struct sync_timer *t = &sync_timer;
    return nrfx_timer_capture_get(&t->timer,
                                  SYNC_TIMER_CAPTURE_CHANNEL_USB_SOF);
}

qu32_32 sync_timer_get_central_time(void) {
    struct sync_timer *t = &sync_timer;
    qu32_32 time = qu32_32_from_int(
        nrfx_timer_capture(&t->timer, SYNC_TIMER_CAPTURE_CHANNEL_MANUAL));
    sync_timer_local_to_central(&time);
    return time;
}

bool sync_timer_local_to_central(qu32_32 *time) {
    struct sync_timer *t = &sync_timer;
    if (!t->init) return false;

    if (t->freq_est.status != FREQ_EST_STATUS_RESET) {
        qu32_32 theta = freq_est_predict(&t->freq_est, *time);
        *time -= theta;
        return true;
    } else {
        return false;
    }
}