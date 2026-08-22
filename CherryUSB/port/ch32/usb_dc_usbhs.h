/**
 * @file usb_dc_usbhs.h
 * @brief CH32 USBHS device-controller service helpers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USB_DC_USBHS_H
#define USB_DC_USBHS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run one bounded controller watchdog service step.
 *
 * @param now_ticks Monotonic 1 MHz counter value.
 * @return 1 when stale EP0 priority state was released, otherwise 0.
 */
uint8_t usb_dc_usbhs_service(uint16_t now_ticks);

/**
 * @brief Recover an IN endpoint whose completion notification was lost.
 *
 * @param ep IN endpoint address.
 * @return 1 when an armed transfer was recovered, 0 when completion is
 *         pending or no transfer is armed, and a negative value on error.
 */
int usb_dc_usbhs_recover_in(uint8_t ep);

/** @brief Mask USBHS interrupts around a main-context mailbox copy. */
void usb_dc_usbhs_irq_lock(void);

/** @brief Restore USBHS interrupts after a mailbox copy. */
void usb_dc_usbhs_irq_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_DC_USBHS_H */
