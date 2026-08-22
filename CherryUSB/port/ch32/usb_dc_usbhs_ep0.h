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

typedef struct
{
    uint16_t deferred_in_mask;
    uint8_t active;
    uint8_t status_token;
    uint8_t generation;
} ch32_usbhs_ep0_priority_state_t;

#define CH32_USBHS_SUSPEND_NO_CHANGE 0U
#define CH32_USBHS_SUSPEND_ENTER     1U
#define CH32_USBHS_SUSPEND_LEAVE     2U

/** @brief Check whether a wrap-safe 16-bit deadline has expired. */
static inline uint8_t ch32_usbhs_deadline_due(uint16_t now,
                                              uint16_t deadline)
{
    return ((int16_t)(now - deadline) >= 0) ? 1U : 0U;
}

/** @brief Check whether a deferred IN endpoint may safely return to ACK. */
static inline uint8_t ch32_usbhs_in_may_resume(uint8_t enabled,
                                               uint8_t armed,
                                               uint8_t stalled)
{
    return (enabled && armed && !stalled) ? 1U : 0U;
}

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

/** @brief Reset EP0 priority arbitration state. */
static inline void ch32_usbhs_ep0_priority_init(
    volatile ch32_usbhs_ep0_priority_state_t *state)
{
    state->deferred_in_mask = 0U;
    state->active = 0U;
    state->status_token = PID_OUT;
    state->generation = 0U;
}

/** @brief Start a control request and select its final status token. */
static inline void ch32_usbhs_ep0_priority_begin(
    volatile ch32_usbhs_ep0_priority_state_t *state,
    uint8_t device_to_host)
{
    state->active = 1U;
    state->status_token = device_to_host ? PID_OUT : PID_IN;
    state->generation++;
}

/** @brief Mark one non-control IN endpoint for resume after EP0. */
static inline void ch32_usbhs_ep0_priority_defer_in(
    volatile ch32_usbhs_ep0_priority_state_t *state, uint8_t ep_idx)
{
    if((ep_idx > 0U) && (ep_idx < 16U))
    {
        state->deferred_in_mask |= (uint16_t)(1UL << ep_idx);
    }
}

/** @brief Finish EP0 only on its zero-length status transaction. */
static inline uint16_t ch32_usbhs_ep0_priority_finish(
    volatile ch32_usbhs_ep0_priority_state_t *state,
    uint8_t token, uint16_t transfer_length)
{
    uint16_t deferred_in_mask;

    if(!state->active || (token != state->status_token) ||
       (transfer_length != 0U))
    {
        return 0U;
    }

    deferred_in_mask = state->deferred_in_mask;
    state->deferred_in_mask = 0U;
    state->active = 0U;
    state->generation++;
    return deferred_in_mask;
}

/** @brief Abort a stalled control request and release deferred IN endpoints. */
static inline uint16_t ch32_usbhs_ep0_priority_abort(
    volatile ch32_usbhs_ep0_priority_state_t *state)
{
    uint16_t deferred_in_mask = state->deferred_in_mask;

    state->deferred_in_mask = 0U;
    state->active = 0U;
    state->generation++;
    return deferred_in_mask;
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
