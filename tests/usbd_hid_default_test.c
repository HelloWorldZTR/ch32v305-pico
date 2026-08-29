/**
 * @file usbd_hid_default_test.c
 * @brief Verify that an unhandled HID SET_REPORT is acknowledged by default.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdint.h>

#include "usbd_core.h"
#include "usbd_hid.h"

void usbd_class_register(usbd_class_t *devclass)
{
    (void)devclass;
}

void usbd_class_add_interface(usbd_class_t *devclass, usbd_interface_t *intf)
{
    (void)devclass;
    (void)intf;
}

#include "../CherryUSB/class/hid/usbd_hid.c"

/** @brief Submit a generic keyboard LED-style SET_REPORT request. */
int main(void)
{
    usbd_class_t hid_class = {0};
    usbd_interface_t hid_interface = {0};
    struct usb_setup_packet setup = {0};
    uint8_t report = 0x02U;
    uint8_t *data = &report;
    uint32_t len = 1U;

    hid_interface.intf_num = 0U;
    usbd_hid_add_interface(&hid_class, &hid_interface);
    setup.bmRequestType = USB_REQUEST_DIR_OUT |
                          USB_REQUEST_CLASS |
                          USB_REQUEST_RECIPIENT_INTERFACE;
    setup.bRequest = HID_REQUEST_SET_REPORT;
    setup.wValue = (uint16_t)(HID_REPORT_OUTPUT << 8);
    setup.wIndex = 0U;
    setup.wLength = 1U;

    assert(hid_interface.class_handler(&setup, &data, &len) == 0);
    return 0;
}
