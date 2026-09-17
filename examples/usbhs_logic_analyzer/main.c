/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "capture.h"
#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"
#include "sump_protocol.h"
#include "usbd_core.h"
#include "usbd_cdc.h"

#define CDC_DATA_EP 0x01U
#define CDC_OUT_EP  CDC_DATA_EP
#define CDC_IN_EP   (0x80U | CDC_DATA_EP)
#define CDC_INT_EP  0x82U
#define CDC_MPS     512U

#define USB_VID          0x1a86U
#define USB_PID          0xfe14U
#define USB_BCD_DEVICE   0x0100U
#define USB_MAX_POWER    100U
#define USB_LANGID_EN_US 0x0409U
#define USB_CONFIG_SIZE  (9U + CDC_ACM_DESCRIPTOR_LEN)

static const uint8_t cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xef, 0x02, 0x01,
                               USB_VID, USB_PID, USB_BCD_DEVICE, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01,
                               USB_CONFIG_BUS_POWERED, USB_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, 0x02),

    USB_LANGID_INIT(USB_LANGID_EN_US),

    0x08, USB_DESCRIPTOR_TYPE_STRING,
    'W', 0, 'C', 0, 'H', 0,

    0x3a, USB_DESCRIPTOR_TYPE_STRING,
    'C', 0, 'H', 0, '3', 0, '2', 0, 'V', 0, '3', 0, '0', 0, '5', 0,
    ' ', 0, 'P', 0, 'i', 0, 'c', 0, 'o', 0, ' ', 0,
    'L', 0, 'o', 0, 'g', 0, 'i', 0, 'c', 0, ' ', 0,
    'A', 0, 'n', 0, 'a', 0, 'l', 0, 'y', 0, 'z', 0, 'e', 0, 'r', 0,

    0x12, USB_DESCRIPTOR_TYPE_STRING,
    '3', 0, '0', 0, '5', 0, 'L', 0, 'A', 0, '0', 0, '0', 0, '1', 0,

    0x0a, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0xef, 0x02, 0x01, 0x40, 0x01, 0x00,
    0x00
};

static usbd_class_t cdc_class;
static usbd_interface_t cdc_command_interface;
static usbd_interface_t cdc_data_interface;

static struct sump_parser sump;
static uint8_t rx_packet[CDC_MPS];
static volatile uint16_t rx_length;
static volatile uint8_t rx_pending;
static uint8_t tx_packet[CDC_MPS];
static volatile uint8_t tx_busy;
static uint32_t tx_sample_cursor;
static uint32_t tx_samples_remaining;
static uint8_t tx_capture_active;

static void status_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_8;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(GPIOA, GPIO_Pin_8);
}

void usb_dc_low_level_init(void)
{
    RCC_HSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET) {
    }
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSI);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    NVIC_EnableIRQ(USBHS_IRQn);
}

void usb_dc_low_level_deinit(void)
{
    NVIC_DisableIRQ(USBHS_IRQn);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, DISABLE);
    RCC_USBHSPHYPLLALIVEcmd(DISABLE);
}

static void cdc_out_callback(uint8_t ep)
{
    uint32_t received = 0U;

    if (rx_pending == 0U) {
        if (usbd_ep_read(ep, rx_packet, sizeof(rx_packet), &received) == 0) {
            rx_length = (uint16_t)received;
            rx_pending = 1U;
        }
    }
}

static void cdc_in_callback(uint8_t ep)
{
    (void)ep;
    tx_busy = 0U;
}

static usbd_endpoint_t cdc_out_endpoint = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = cdc_out_callback
};

static usbd_endpoint_t cdc_in_endpoint = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = cdc_in_callback
};

static void usb_init(void)
{
    usbd_desc_register(cdc_descriptor);
    usbd_cdc_add_acm_interface(&cdc_class, &cdc_command_interface);
    usbd_cdc_add_acm_interface(&cdc_class, &cdc_data_interface);
    usbd_interface_add_endpoint(&cdc_data_interface, &cdc_out_endpoint);
    usbd_interface_add_endpoint(&cdc_data_interface, &cdc_in_endpoint);
    usbd_initialize();
}

static void queue_response(const uint8_t *data, uint16_t length)
{
    if (tx_busy != 0U || length > sizeof(tx_packet)) {
        return;
    }
    memcpy(tx_packet, data, length);
    if (usbd_ep_write(CDC_IN_EP, tx_packet, length, NULL) == 0) {
        tx_busy = 1U;
    }
}

static void begin_capture_transmit(void)
{
    const struct capture_result *capture = capture_get_result();

    tx_sample_cursor = capture->end_sample;
    tx_samples_remaining = capture->sample_count;
    tx_capture_active = 1U;
    GPIO_ResetBits(GPIOA, GPIO_Pin_8);
}

static void service_capture_transmit(void)
{
    uint16_t length;
    uint16_t i;

    if (tx_capture_active == 0U || tx_busy != 0U) {
        return;
    }
    if (tx_samples_remaining == 0U) {
        tx_capture_active = 0U;
        capture_abort();
        return;
    }

    length = tx_samples_remaining > CDC_MPS ? CDC_MPS :
                                                  (uint16_t)tx_samples_remaining;
    for (i = 0U; i < length; i++) {
        tx_packet[i] = capture_get_sample(--tx_sample_cursor);
    }
    if (usbd_ep_write(CDC_IN_EP, tx_packet, length, NULL) == 0) {
        tx_busy = 1U;
        tx_samples_remaining -= length;
    }
}

static void handle_sump_action(enum sump_action action)
{
    static const uint8_t identification[4] = {'1', 'A', 'L', 'S'};
    uint8_t metadata[96];
    size_t metadata_length;

    switch (action) {
    case SUMP_ACTION_RESET:
        capture_abort();
        tx_capture_active = 0U;
        tx_samples_remaining = 0U;
        GPIO_ResetBits(GPIOA, GPIO_Pin_8);
        break;
    case SUMP_ACTION_RUN:
        if (tx_busy == 0U) {
            capture_start(&sump.config);
            GPIO_SetBits(GPIOA, GPIO_Pin_8);
        }
        break;
    case SUMP_ACTION_SEND_ID:
        queue_response(identification, sizeof(identification));
        break;
    case SUMP_ACTION_SEND_METADATA:
        metadata_length = sump_build_metadata(metadata, sizeof(metadata),
                                              CAPTURE_ADVERTISED_BYTES,
                                              CAPTURE_MAX_RATE_HZ);
        if (metadata_length != 0U) {
            queue_response(metadata, (uint16_t)metadata_length);
        }
        break;
    case SUMP_ACTION_NONE:
    default:
        break;
    }
}

static void service_receive(void)
{
    uint16_t length;
    uint16_t i;

    if (rx_pending == 0U) {
        return;
    }
    length = rx_length;
    for (i = 0U; i < length; i++) {
        handle_sump_action(sump_parser_feed(&sump, rx_packet[i]));
    }
    rx_pending = 0U;
    rx_length = 0U;
    usbd_ep_read(CDC_OUT_EP, NULL, 0U, NULL);
}

int main(void)
{
    SystemCoreClockUpdate();
    status_led_init();
    capture_init();
    sump_parser_init(&sump);
    usb_init();

    while (1) {
        service_receive();
        capture_poll();
        if (capture_get_state() == CAPTURE_COMPLETE &&
            tx_capture_active == 0U) {
            begin_capture_transmit();
        } else if (capture_get_state() == CAPTURE_OVERFLOW) {
            /* A solid LED and host timeout are deliberate: SUMP has no error
             * response that PulseView understands. RESET remains available. */
            GPIO_SetBits(GPIOA, GPIO_Pin_8);
        }
        service_capture_transmit();
    }
}
