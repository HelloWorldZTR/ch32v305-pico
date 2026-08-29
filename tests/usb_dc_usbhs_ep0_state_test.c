/**
 * @file usb_dc_usbhs_ep0_state_test.c
 * @brief Portable CH32 USBHS EP0 toggle and suspend-state regression test.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdint.h>

#include "usb_dc_usbhs_ep0.h"

/** @brief Verify generic EP0 handshake and suspend transition helpers. */
int main(void)
{
    uint8_t rx_ctrl;

    rx_ctrl = USBHS_EP_R_TOG_1 | USBHS_EP_R_RES_NAK;
    rx_ctrl = ch32_usbhs_ep0_rx_apply_handshake(rx_ctrl, 0U);
    assert((rx_ctrl & USBHS_EP_R_TOG_MASK) == USBHS_EP_R_TOG_1);
    assert((rx_ctrl & USBHS_EP_R_RES_MASK) == USBHS_EP_R_RES_ACK);

    rx_ctrl = ch32_usbhs_ep0_rx_advance_toggle(rx_ctrl);
    rx_ctrl = ch32_usbhs_ep0_rx_apply_handshake(rx_ctrl, 1U);
    assert((rx_ctrl & USBHS_EP_R_TOG_MASK) == USBHS_EP_R_TOG_0);
    assert((rx_ctrl & USBHS_EP_R_RES_MASK) == USBHS_EP_R_RES_NAK);

    rx_ctrl = USBHS_EP_R_TOG_1 | USBHS_EP_R_RES_STALL;
    assert(ch32_usbhs_ep0_rx_apply_handshake(rx_ctrl, 0U) == rx_ctrl);

    assert(ch32_usbhs_suspend_transition(0U, 1U) ==
           CH32_USBHS_SUSPEND_ENTER);
    assert(ch32_usbhs_suspend_transition(1U, 1U) ==
           CH32_USBHS_SUSPEND_NO_CHANGE);
    assert(ch32_usbhs_suspend_transition(1U, 0U) ==
           CH32_USBHS_SUSPEND_LEAVE);
    return 0;
}
