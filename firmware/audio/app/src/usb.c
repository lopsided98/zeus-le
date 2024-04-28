// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(usb);

#define USB_VID_PID_CODES 0x1209
#define USB_PID_PID_CODES_TEST 0x000a

USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   USB_VID_PID_CODES, USB_PID_PID_CODES_TEST);

USBD_DESC_LANG_DEFINE(usb_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb_mfr, "Zeus LE");
USBD_DESC_PRODUCT_DEFINE(usb_product, "Zeus LE Audio");
USBD_DESC_SERIAL_NUMBER_DEFINE(usb_sn, "0000");

USBD_CONFIGURATION_DEFINE(usb_config, 0, 125 /* mA */);

static struct usb {
    // Resources
    struct usbd_contex* const ctx;
} usb = {
    .ctx = &usbd,
};

int usb_init(void) {
    struct usb* u = &usb;
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

    STRUCT_SECTION_FOREACH_ALTERNATE(usbd_class_fs, usbd_class_node, node) {
        // Pull everything that is enabled in our configuration.
        ret = usbd_register_class(u->ctx, node->c_data->name, USBD_SPEED_FS, 1);
        if (ret < 0) {
            LOG_ERR("failed to register %s (err %d)", node->c_data->name, ret);
            return ret;
        }

        LOG_DBG("register %s", node->c_data->name);
    }

    STRUCT_SECTION_FOREACH_ALTERNATE(usbd_class_hs, usbd_class_node, node) {
        // Pull everything that is enabled in our configuration.
        ret = usbd_register_class(u->ctx, node->c_data->name, USBD_SPEED_HS, 1);
        if (ret < 0) {
            LOG_ERR("failed to register %s (err %d)", node->c_data->name, ret);
            return ret;
        }

        LOG_DBG("register %s", node->c_data->name);
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

    return 0;
}