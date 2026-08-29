/**
 * @file usbd_hid_report_test.c
 * @brief Verify generic multi-byte HID GET_REPORT and SET_REPORT callbacks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdint.h>

#include "usbd_core.h"
#include "usbd_hid.h"

static const uint8_t test_feature_report[37] = {0x02U};
static uint32_t test_set_report_len;

void usbd_class_register(usbd_class_t *devclass)
{
    (void)devclass;
}

void usbd_class_add_interface(usbd_class_t *devclass, usbd_interface_t *intf)
{
    (void)devclass;
    (void)intf;
}

int usbd_hid_get_report(uint8_t intf, uint8_t report_id,
                        uint8_t report_type, const uint8_t **report,
                        uint32_t *report_len)
{
    if((intf != 0U) || (report_id != 0x02U) ||
       (report_type != HID_REPORT_FEATURE))
    {
        return -1;
    }
    *report = test_feature_report;
    *report_len = sizeof(test_feature_report);
    return 0;
}

int usbd_hid_set_report(uint8_t intf, uint8_t report_id,
                        uint8_t report_type, uint8_t *report,
                        uint32_t report_len)
{
    (void)report;
    if((intf != 0U) || (report_id != 0x05U) ||
       (report_type != HID_REPORT_OUTPUT))
    {
        return -1;
    }
    test_set_report_len = report_len;
    return 0;
}

/** @brief Exercise report callbacks through the HID class handler. */
int main(void)
{
    usbd_class_t hid_class = {0};
    usbd_interface_t hid_interface = {0};
    struct usb_setup_packet setup = {0};
    uint8_t output_report[300] = {0};
    uint8_t *data = output_report;
    uint32_t len = sizeof(output_report);

    hid_interface.intf_num = 0U;
    usbd_hid_add_interface(&hid_class, &hid_interface);

    setup.bmRequestType = USB_REQUEST_DIR_IN |
                          USB_REQUEST_CLASS |
                          USB_REQUEST_RECIPIENT_INTERFACE;
    setup.bRequest = HID_REQUEST_GET_REPORT;
    setup.wValue = (uint16_t)((HID_REPORT_FEATURE << 8) | 0x02U);
    setup.wIndex = 0U;
    setup.wLength = 64U;
    assert(hid_interface.class_handler(&setup, &data, &len) == 0);
    assert(data == test_feature_report);
    assert(len == sizeof(test_feature_report));

    setup.bmRequestType = USB_REQUEST_DIR_OUT |
                          USB_REQUEST_CLASS |
                          USB_REQUEST_RECIPIENT_INTERFACE;
    setup.bRequest = HID_REQUEST_SET_REPORT;
    setup.wValue = (uint16_t)((HID_REPORT_OUTPUT << 8) | 0x05U);
    setup.wLength = sizeof(output_report);
    data = output_report;
    len = sizeof(output_report);
    assert(hid_interface.class_handler(&setup, &data, &len) == 0);
    assert(test_set_report_len == sizeof(output_report));
    return 0;
}
