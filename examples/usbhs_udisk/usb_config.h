#ifndef USB_CONFIG_H
#define USB_CONFIG_H

/* The Makefile selects the PB6/PB7 controller in 480 Mbit/s HS mode. */
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 256

/* This descriptor uses only EP0, EP2 IN and EP3 OUT.  The CH32 port allocates
 * one 512-byte RX and TX DMA buffer per non-control endpoint, so keeping the
 * default value of 16 would waste 12 KiB of this chip's 32 KiB SRAM. */
#define USB_NUM_BIDIR_ENDPOINTS 4

/* Keep the firmware small and avoid requiring a debug UART. */
#define CONFIG_USB_DBG_LEVEL 0
#define CONFIG_USB_PRINTF(...) ((void)0)

#define CONFIG_USBDEV_MSC_MANUFACTURER_STRING "WCH"
#define CONFIG_USBDEV_MSC_PRODUCT_STRING      "CH32V305 USBHS MSC"
#define CONFIG_USBDEV_MSC_VERSION_STRING      "1.00"

#endif
