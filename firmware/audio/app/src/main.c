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
#include <zephyr/settings/settings.h>

#include "audio.h"
#include "freq_est.h"
#include "ftp.h"
#include "mgr.h"
#include "net_audio.h"
#include "record.h"
#include "sd_card.h"
#include "sync_timer.h"
#include "usb.h"

LOG_MODULE_REGISTER(audio_app);

int main(void) {
    // Initialize the Bluetooth Subsystem
    int ret = bt_enable(NULL);
    if (ret < 0) {
        LOG_ERR("failed to enable Bluetooth (err %d)", ret);
        return 0;
    }

    ret = settings_load();
    if (ret) {
        LOG_WRN("failed to load settings (err %d)", ret);
        // No return, settings failure is not fatal
    }

    usb_init();
    sd_card_init();
    ftp_init();
    net_audio_init();
    sync_timer_init();
    record_init();
    audio_init();
    mgr_init();

    LOG_INF("Booted");

    return 0;
}