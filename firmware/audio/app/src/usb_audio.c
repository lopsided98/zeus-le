// SPDX-License-Identifier: GPL-3.0-or-later

#include "usb_audio.h"

#include <zephyr/usb/class/usbd_uac2.h>

#include "freq_ctlr.h"
#include "freq_est.h"
#include "sync_timer.h"

#define USB_AUDIO_SYNC_INTERVAL 100

static struct usb {
    // Resources
    const struct device *const dev;

    const struct freq_est_config freq_est_cfg;
    struct freq_est freq_est;
    const struct freq_ctlr freq_ctlr;
    struct k_work *const sync_work;

    // State
} usb_audio = {
    .dev = DEVICE_DT_GET(DT_NODELABEL(usb_mic)),

    .freq_est_cfg =
        {
            .nominal_freq = SYNC_TIMER_FREQ,
            .k_u = 1.0 / 44100 * USB_AUDIO_SYNC_INTERVAL,
            .q_theta = 0.0,
            .q_f = 256.0,
            .r = 390625.0,
            .p0 = 1e6,
        },
    .freq_ctlr =
        {
            .k_theta = 4.03747559e-11,
            .k_f = 6.45996094e-05,
            .max_step = 1000,
        },

};

static void usb_audio_sof(const struct device *dev, void *user_data) {}

static void usb_audio_terminal_update(const struct device *dev,
                                      uint8_t terminal, bool enabled,
                                      bool microframes, void *user_data) {}

static void *usb_audio_get_recv_buf(const struct device *dev, uint8_t terminal,
                                    uint16_t size, void *user_data) {
    return NULL;
}

static void usb_audio_data_recv(const struct device *dev, uint8_t terminal,
                                void *buf, uint16_t size, void *user_data) {}

static void usb_audio_buf_release(const struct device *dev, uint8_t terminal,
                                  void *buf, void *user_data) {}

static uint32_t usb_audio_feedback(const struct device *dev, uint8_t terminal,
                                   void *user_data) {
    return (44100 << 14) / 1000;
}

static struct uac2_ops usb_audio_ops = {
    .sof_cb = usb_audio_sof,
    .terminal_update_cb = usb_audio_terminal_update,
    .get_recv_buf = usb_audio_get_recv_buf,
    .data_recv_cb = usb_audio_data_recv,
    .buf_release_cb = usb_audio_buf_release,
    .feedback_cb = usb_audio_feedback,
};

int usb_audio_init(void) {
    struct usb *a = &usb_audio;

    usbd_uac2_set_ops(a->dev, &usb_audio_ops, NULL);

    return 0;
}