/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef USB_CONFIG_H
#define USB_CONFIG_H

#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 256

/* EP1 is bidirectional CDC data, EP2 is the CDC notification endpoint. */
#define USB_NUM_BIDIR_ENDPOINTS 3

#define CONFIG_USB_DBG_LEVEL 0
#define CONFIG_USB_PRINTF(...) ((void)0)

#endif
