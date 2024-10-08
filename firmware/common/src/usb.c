// SPDX-License-Identifier: GPL-3.0-or-later

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(usb);

#define USB_VID_PID_CODES 0x1209
#define USB_PID_PID_CODES_TEST 0x000a

USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   USB_VID_PID_CODES, USB_PID_PID_CODES_TEST);

USBD_DESC_LANG_DEFINE(usb_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb_mfr, "Zeus LE");
USBD_DESC_PRODUCT_DEFINE(usb_product, "Zeus LE");
USBD_DESC_SERIAL_NUMBER_DEFINE(usb_sn);

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");

USBD_CONFIGURATION_DEFINE(usb_config, 0, 100 /* mA */, &fs_cfg_desc);

SHELL_UART_DEFINE(usb_shell_transport);
SHELL_DEFINE(usb_shell, "usb:~$ ", &usb_shell_transport,
             CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_SIZE,
             0,  // Don't wait. The queue fills up when the USB serial port is
                 // not open, and waiting blocks all the other log backends.
             SHELL_FLAG_OLF_CRLF);

static struct usb {
    // Resources
    struct usbd_context* const ctx;
    const struct device* const shell_dev;

    // State
    bool init;
} usb = {
    .ctx = &usbd,
    .shell_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0)),
};

static int usb_enable_shell(void) {
    struct usb* u = &usb;

    if (!device_is_ready(u->shell_dev)) {
        LOG_ERR("USB shell device not ready");
        return -ENODEV;
    }

    const uint32_t level =
        (CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > LOG_LEVEL_DBG)
            ? CONFIG_LOG_MAX_LEVEL
            : CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL;
    const struct shell_backend_config_flags flags =
        SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;
    int ret = shell_init(&usb_shell, u->shell_dev, flags, true, level);
    if (ret < 0) {
        LOG_ERR("failed to initialize USB shell (err %d)", ret);
        return ret;
    }

    return 0;
}

int usb_init(void) {
    struct usb* u = &usb;
    if (u->init) return -EALREADY;

    int ret;

    ret = usbd_add_descriptor(u->ctx, &usb_lang);
    if (ret < 0) {
        LOG_ERR("failed to add USB language descriptor (err %d)", ret);
        return ret;
    }

    ret = usbd_add_descriptor(u->ctx, &usb_mfr);
    if (ret < 0) {
        LOG_ERR("failed to add USB manufacturer descriptor (err %d)", ret);
        return ret;
    }

    ret = usbd_add_descriptor(u->ctx, &usb_product);
    if (ret < 0) {
        LOG_ERR("failed to add USB product descriptor (err %d)", ret);
        return ret;
    }

    ret = usbd_add_descriptor(u->ctx, &usb_sn);
    if (ret < 0) {
        LOG_ERR("failed to add USB SN descriptor (err %d)", ret);
        return ret;
    }

    ret = usbd_add_configuration(u->ctx, USBD_SPEED_FS, &usb_config);
    if (ret < 0) {
        LOG_ERR("failed to add USB configuration (err %d)", ret);
        return ret;
    }

    ret = usbd_register_all_classes(u->ctx, USBD_SPEED_FS, 1);
    if (ret) {
        LOG_ERR("failed to register classes (err %d)", ret);
        return ret;
    }

    /*
     * Class with multiple interfaces have an Interface
     * Association Descriptor available, use an appropriate triple
     * to indicate it.
     */
    usbd_device_set_code_triple(u->ctx, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS,
                                0x02, 0x01);

    ret = usbd_init(u->ctx);
    if (ret) {
        LOG_ERR("failed to initialize USB device");
        return ret;
    }

    ret = usbd_enable(u->ctx);
    if (ret) {
        LOG_ERR("failed to enable USB device");
        return ret;
    }

    ret = usb_enable_shell();
    if (ret < 0) {
        return ret;
    }

    u->init = true;
    return 0;
}
