/**
 * @file usbd_core_configuration_test.c
 * @brief Portable multi-configuration and alternate-setting regression test.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "usbd_core.h"

static uint8_t test_open_count[256];
static uint8_t test_close_count[256];
static uint8_t test_stalled[256];

int usb_dc_init(void)
{
    return 0;
}

int usb_dc_deinit(void)
{
    return 0;
}

int usbd_set_address(uint8_t addr)
{
    (void)addr;
    return 0;
}

int usbd_ep_open(const struct usbd_endpoint_cfg *ep_cfg)
{
    test_open_count[ep_cfg->ep_addr]++;
    test_stalled[ep_cfg->ep_addr] = 0U;
    return 0;
}

int usbd_ep_close(uint8_t ep)
{
    test_close_count[ep]++;
    test_stalled[ep] = 0U;
    return 0;
}

int usbd_ep_set_stall(uint8_t ep)
{
    test_stalled[ep] = 1U;
    return 0;
}

int usbd_ep_clear_stall(uint8_t ep)
{
    test_stalled[ep] = 0U;
    return 0;
}

int usbd_ep_is_stalled(uint8_t ep, uint8_t *stalled)
{
    *stalled = test_stalled[ep];
    return 0;
}

int usbd_ep_write(uint8_t ep, const uint8_t *data, uint32_t data_len,
                  uint32_t *ret_bytes)
{
    (void)ep;
    (void)data;
    if(ret_bytes != NULL)
    {
        *ret_bytes = data_len;
    }
    return 0;
}

int usbd_ep_read(uint8_t ep, uint8_t *data, uint32_t max_data_len,
                 uint32_t *read_bytes)
{
    (void)ep;
    (void)data;
    (void)max_data_len;
    (void)read_bytes;
    return 0;
}

#include "../CherryUSB/core/usbd_core.c"

#define TEST_CONFIG_TOTAL_LENGTH 41U

static const uint8_t test_descriptors[] = {
    0x12U, USB_DESCRIPTOR_TYPE_DEVICE,
    0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x40U,
    0x34U, 0x12U, 0x78U, 0x56U, 0x00U, 0x01U,
    0x01U, 0x02U, 0x03U, 0x02U,

    0x09U, USB_DESCRIPTOR_TYPE_CONFIGURATION,
    TEST_CONFIG_TOTAL_LENGTH, 0x00U, 0x01U, 0x01U, 0x00U,
    USB_CONFIG_BUS_POWERED, 0x32U,
    0x09U, USB_DESCRIPTOR_TYPE_INTERFACE,
    0x00U, 0x00U, 0x01U, 0xffU, 0x00U, 0x00U, 0x00U,
    0x07U, USB_DESCRIPTOR_TYPE_ENDPOINT,
    0x81U, USB_ENDPOINT_TYPE_INTERRUPT, 0x40U, 0x00U, 0x01U,
    0x09U, USB_DESCRIPTOR_TYPE_INTERFACE,
    0x00U, 0x01U, 0x01U, 0xffU, 0x00U, 0x00U, 0x00U,
    0x07U, USB_DESCRIPTOR_TYPE_ENDPOINT,
    0x82U, USB_ENDPOINT_TYPE_INTERRUPT, 0x40U, 0x00U, 0x01U,

    0x09U, USB_DESCRIPTOR_TYPE_CONFIGURATION,
    TEST_CONFIG_TOTAL_LENGTH, 0x00U, 0x01U, 0x02U, 0x00U,
    USB_CONFIG_BUS_POWERED, 0x32U,
    0x09U, USB_DESCRIPTOR_TYPE_INTERFACE,
    0x00U, 0x00U, 0x01U, 0xffU, 0x00U, 0x00U, 0x00U,
    0x07U, USB_DESCRIPTOR_TYPE_ENDPOINT,
    0x83U, USB_ENDPOINT_TYPE_INTERRUPT, 0x40U, 0x00U, 0x01U,
    0x09U, USB_DESCRIPTOR_TYPE_INTERFACE,
    0x00U, 0x01U, 0x01U, 0xffU, 0x00U, 0x00U, 0x00U,
    0x07U, USB_DESCRIPTOR_TYPE_ENDPOINT,
    0x84U, USB_ENDPOINT_TYPE_INTERRUPT, 0x40U, 0x00U, 0x01U,
    0x00U
};

static bool test_device_request(uint8_t request, uint16_t value,
                                uint8_t **data, uint32_t *len)
{
    struct usb_setup_packet setup = {0};

    setup.bmRequestType = USB_REQUEST_DIR_OUT |
                          USB_REQUEST_STANDARD |
                          USB_REQUEST_RECIPIENT_DEVICE;
    setup.bRequest = request;
    setup.wValue = value;
    return usbd_std_device_req_handler(&setup, data, len);
}

static bool test_interface_request(uint8_t request, uint16_t value,
                                   uint8_t **data, uint32_t *len)
{
    struct usb_setup_packet setup = {0};

    setup.bmRequestType = USB_REQUEST_STANDARD |
                          USB_REQUEST_RECIPIENT_INTERFACE;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = 0U;
    return usbd_std_interface_req_handler(&setup, data, len);
}

static bool test_endpoint_status(uint8_t ep, uint8_t **data, uint32_t *len)
{
    struct usb_setup_packet setup = {0};

    setup.bmRequestType = USB_REQUEST_DIR_IN |
                          USB_REQUEST_STANDARD |
                          USB_REQUEST_RECIPIENT_ENDPOINT;
    setup.bRequest = USB_REQUEST_GET_STATUS;
    setup.wIndex = ep;
    return usbd_std_endpoint_req_handler(&setup, data, len);
}

/** @brief Exercise configuration and alternate-setting state transitions. */
int main(void)
{
    usbd_class_t test_class = {0};
    usbd_interface_t test_interface = {0};
    uint8_t *data = usbd_core_cfg.req_data;
    uint32_t len = 0U;

    usbd_class_register(&test_class);
    usbd_class_add_interface(&test_class, &test_interface);
    usbd_desc_register(test_descriptors);

    assert(test_device_request(USB_REQUEST_SET_CONFIGURATION, 1U,
                               &data, &len));
    assert(test_open_count[0x81U] == 1U);
    assert(test_open_count[0x82U] == 0U);
    assert(test_open_count[0x83U] == 0U);

    assert(test_interface_request(USB_REQUEST_SET_INTERFACE, 1U,
                                  &data, &len));
    assert(test_close_count[0x81U] == 1U);
    assert(test_open_count[0x82U] == 1U);
    assert(test_interface_request(USB_REQUEST_GET_INTERFACE, 0U,
                                  &data, &len));
    assert(len == 1U && data[0] == 1U);
    assert(!test_endpoint_status(0x81U, &data, &len));
    assert(test_endpoint_status(0x82U, &data, &len));

    assert(test_device_request(USB_REQUEST_SET_CONFIGURATION, 2U,
                               &data, &len));
    assert(test_close_count[0x82U] == 1U);
    assert(test_open_count[0x83U] == 1U);
    assert(test_open_count[0x84U] == 0U);
    assert(!test_endpoint_status(0x81U, &data, &len));
    assert(test_endpoint_status(0x83U, &data, &len));

    assert(test_interface_request(USB_REQUEST_SET_INTERFACE, 1U,
                                  &data, &len));
    assert(test_close_count[0x83U] == 1U);
    assert(test_open_count[0x84U] == 1U);

    assert(test_device_request(USB_REQUEST_SET_CONFIGURATION, 0U,
                               &data, &len));
    assert(test_close_count[0x84U] == 1U);
    assert(!usb_device_is_configured());
    return 0;
}
