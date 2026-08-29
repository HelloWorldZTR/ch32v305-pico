/**
 * @file usb_dc_usbhs_ep0.h
 * @brief Small, testable CH32 USBHS endpoint-zero handshake helpers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USB_DC_USBHS_EP0_H
#define USB_DC_USBHS_EP0_H

#include <stdint.h>

#include "usb_ch32_usbhs_reg.h"

#define CH32_USBHS_SUSPEND_NO_CHANGE 0U
#define CH32_USBHS_SUSPEND_ENTER     1U
#define CH32_USBHS_SUSPEND_LEAVE     2U

/** @brief Classify one de-duplicated hardware suspend-state transition. */
static inline uint8_t ch32_usbhs_suspend_transition(uint8_t previous,
                                                    uint8_t current)
{
    if(previous == current)
    {
        return CH32_USBHS_SUSPEND_NO_CHANGE;
    }
    return current ? CH32_USBHS_SUSPEND_ENTER : CH32_USBHS_SUSPEND_LEAVE;
}

/**
 * @brief Advance the expected PID after one accepted EP0 OUT packet.
 *
 * @param rx_ctrl Current UEP0_RX_CTRL value.
 *
 * @return UEP0_RX_CTRL with DATA0/DATA1 advanced once.
 */
static inline uint8_t ch32_usbhs_ep0_rx_advance_toggle(uint8_t rx_ctrl)
{
    return (uint8_t)(rx_ctrl ^ USBHS_EP_R_TOG_1);
}

/**
 * @brief Apply the EP0 RX response without disturbing its data toggle.
 *
 * @param rx_ctrl Current UEP0_RX_CTRL value.
 * @param tx_armed Non-zero when a control IN or status packet is armed.
 *
 * @return Updated UEP0_RX_CTRL value, or the original STALL response.
 */
static inline uint8_t ch32_usbhs_ep0_rx_apply_handshake(uint8_t rx_ctrl,
                                                        uint8_t tx_armed)
{
    if((rx_ctrl & USBHS_EP_R_RES_MASK) == USBHS_EP_R_RES_STALL)
    {
        return rx_ctrl;
    }

    rx_ctrl &= (uint8_t)~USBHS_EP_R_RES_MASK;
    rx_ctrl |= tx_armed ? USBHS_EP_R_RES_NAK : USBHS_EP_R_RES_ACK;
    return rx_ctrl;
}

#endif /* USB_DC_USBHS_EP0_H */
