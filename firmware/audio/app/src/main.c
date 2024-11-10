// SPDX-License-Identifier: GPL-3.0-or-later
#include <nrfx_clock.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "audio.h"
#include "ftp.h"
#include "mgr.h"
#include "record.h"
#include "sd_card.h"
#include "sync_timer.h"
#include "zeus/led.h"
#include "zeus/power.h"
#include "zeus/usb.h"
#include "zeus/wifi.h"

LOG_MODULE_REGISTER(audio_app);

WIFI_POWER_OFF_REGISTER();

#if IS_ENABLED(CONFIG_SOC_SERIES_NRF53X)
static int cpu_clock_128_mhz(void) {
    nrfx_err_t err =
        nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
    if (err != NRFX_SUCCESS) {
        LOG_WRN("Failed to set CPU to 128 MHz: %d", err - NRFX_ERROR_BASE_NUM);
        return -1;
    }
    return 0;
}
#endif

int main(void) {
    int ret;

    ret = power_init();
    if (ret) {
        LOG_ERR("power init failed (err %d)", ret);
    }

#if IS_ENABLED(CONFIG_SOC_SERIES_NRF53X)
    // Set CPU clock to 128 MHz
    cpu_clock_128_mhz();
#endif

    ret = led_boot();
    if (ret < 0) {
        LOG_ERR("failed to set LED (err %d)", ret);
    }

    // Initialize the Bluetooth Subsystem
    ret = bt_enable(NULL);
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
    sync_timer_init();
    record_init();
    audio_init();
    mgr_init();

    LOG_INF("Booted");

    return 0;
}