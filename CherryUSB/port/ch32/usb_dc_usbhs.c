#include "usbd_core.h"
#include "usb_ch32_usbhs_reg.h"
#include "usb_dc_usbhs.h"
#include "usb_dc_usbhs_ep0.h"

#ifndef USBD_IRQHandler
#define USBD_IRQHandler USBHS_IRQHandler //use actual usb irq name instead
#endif

#ifndef USB_NUM_BIDIR_ENDPOINTS
#define USB_NUM_BIDIR_ENDPOINTS 16
#endif

#define USB_DC_USBHS_EP0_TIMEOUT_TICKS 20000U

#define USB_SET_RX_DMA(ep_idx, addr) (*(volatile uint32_t *)((uint32_t)(&USBHS_DEVICE->UEP1_RX_DMA) + 4 * (ep_idx - 1)) = addr)
#define USB_SET_TX_DMA(ep_idx, addr) (*(volatile uint32_t *)((uint32_t)(&USBHS_DEVICE->UEP1_TX_DMA) + 4 * (ep_idx - 1)) = addr)
#define USB_SET_MAX_LEN(ep_idx, len) (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_MAX_LEN) + 4 * ep_idx) = len)
#define USB_SET_TX_LEN(ep_idx, len)  (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_LEN) + 4 * ep_idx) = len)
#define USB_GET_TX_LEN(ep_idx)       (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_LEN) + 4 * ep_idx))
#define USB_SET_TX_CTRL(ep_idx, val) (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_CTRL) + 4 * ep_idx) = val)
#define USB_GET_TX_CTRL(ep_idx)      (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_CTRL) + 4 * ep_idx))
#define USB_SET_RX_CTRL(ep_idx, val) (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_RX_CTRL) + 4 * ep_idx) = val)
#define USB_GET_RX_CTRL(ep_idx)      (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_RX_CTRL) + 4 * ep_idx))

/* Endpoint state */
struct ch32_usbhs_ep_state {
    /** Endpoint max packet size */
    uint16_t ep_mps;
    /** Endpoint Transfer Type.
     * May be Bulk, Interrupt, Control or Isochronous
     */
    uint8_t ep_type;
    uint8_t ep_stalled; /** Endpoint stall flag */
    uint8_t ep_enabled; /** Endpoint hardware enable state */
    uint8_t ep_armed;   /** Endpoint has a transfer offered to the SIE */
};

/* Driver state */
struct ch32_usbhs_udc {
    volatile uint8_t dev_addr;
    struct ch32_usbhs_ep_state in_ep[USB_NUM_BIDIR_ENDPOINTS];                              /*!< IN endpoint parameters*/
    struct ch32_usbhs_ep_state out_ep[USB_NUM_BIDIR_ENDPOINTS];                             /*!< OUT endpoint parameters */
    __attribute__((aligned(4))) uint8_t ep_databuf[USB_NUM_BIDIR_ENDPOINTS - 1][512 + 512]; //epx_out(512)+epx_in(512)
} g_ch32_usbhs_udc;

// clang-format off
/* Endpoint Buffer */
__attribute__ ((aligned(4))) uint8_t EP0_DatabufHD[64]; //ep0(64)
__attribute__ ((aligned(4))) uint8_t EP1_DatabufHD[512+512];  //ep1_out(64)+ep1_in(64)
__attribute__ ((aligned(4))) uint8_t EP2_DatabufHD[512+512];  //ep2_out(64)+ep2_in(64)
// clang-format on

void USBHS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

volatile uint8_t mps_over_flag = 0;
static volatile uint8_t ep0_tx_armed = 0;
static volatile ch32_usbhs_ep0_priority_state_t ep0_priority_state;
static volatile uint8_t ep0_priority_watchdog_started;
static volatile uint16_t ep0_priority_deadline;
static volatile uint8_t usbhs_suspended;
static volatile uint8_t usbhs_saved_int_enable;
volatile bool epx_data_toggle[USB_NUM_BIDIR_ENDPOINTS - 1];

/**
 * @brief Wait briefly after forcing the USBHS SIE reset.
 */
static void usb_dc_usbhs_sie_reset_delay(void)
{
    volatile uint32_t delay;

    for (delay = 0; delay < 800U; delay++) {
        __asm__ volatile("nop");
    }
}

/**
 * @brief Prepare endpoint 0 for the first control transfer after attach/reset.
 */
static void usb_dc_usbhs_ep0_prepare(void)
{
    ep0_tx_armed = 0;
    mps_over_flag = 0;

    g_ch32_usbhs_udc.in_ep[0].ep_mps = USB_CTRL_EP_MPS;
    g_ch32_usbhs_udc.in_ep[0].ep_type = USB_ENDPOINT_TYPE_CONTROL;
    g_ch32_usbhs_udc.in_ep[0].ep_stalled = 0;
    g_ch32_usbhs_udc.in_ep[0].ep_enabled = 1U;
    g_ch32_usbhs_udc.in_ep[0].ep_armed = 0U;
    g_ch32_usbhs_udc.out_ep[0].ep_mps = USB_CTRL_EP_MPS;
    g_ch32_usbhs_udc.out_ep[0].ep_type = USB_ENDPOINT_TYPE_CONTROL;
    g_ch32_usbhs_udc.out_ep[0].ep_stalled = 0;
    g_ch32_usbhs_udc.out_ep[0].ep_enabled = 1U;
    g_ch32_usbhs_udc.out_ep[0].ep_armed = 1U;

    /* EP0 must be ready before the host's first GET_DESCRIPTOR request. */
    USBHS_DEVICE->ENDP_CONFIG = USBHS_EP0_T_EN | USBHS_EP0_R_EN;
    USB_SET_MAX_LEN(0, USB_CTRL_EP_MPS);
    USBHS_DEVICE->UEP0_TX_LEN = 0;
    USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_NAK;
    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
}

/**
 * @brief Apply the EP0 handshake selected by the CherryUSB control handler.
 *
 * The core arms EP0 IN by calling usbd_ep_write(), or explicitly stalls it
 * when a request is rejected.  The USBHS ISR must preserve both decisions.
 */
static void usb_dc_usbhs_ep0_finish_callback(void)
{
    uint8_t tx_ctrl;
    uint8_t rx_ctrl;

    tx_ctrl = USBHS_DEVICE->UEP0_TX_CTRL;
    if((tx_ctrl & USBHS_EP_T_RES_MASK) != USBHS_EP_T_RES_STALL)
    {
        tx_ctrl &= (uint8_t)~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK);
        tx_ctrl |= ep0_tx_armed ? USBHS_EP_T_RES_ACK : USBHS_EP_T_RES_NAK;
        tx_ctrl |= USBHS_EP_T_TOG_1;
        USBHS_DEVICE->UEP0_TX_CTRL = tx_ctrl;
    }

    /* Preserve the PID advanced by each accepted EP0 OUT data packet. */
    rx_ctrl = ch32_usbhs_ep0_rx_apply_handshake(
        USBHS_DEVICE->UEP0_RX_CTRL, ep0_tx_armed);
    USBHS_DEVICE->UEP0_RX_CTRL = rx_ctrl;
}

/** @brief Resume non-control IN endpoints deferred behind EP0. */
static void usb_dc_usbhs_resume_in_endpoints(uint16_t endpoint_mask)
{
    uint8_t ctrl;
    uint8_t ep_idx;

    for(ep_idx = 1U; ep_idx < USB_NUM_BIDIR_ENDPOINTS; ep_idx++)
    {
        if((endpoint_mask & (uint16_t)(1UL << ep_idx)) == 0U)
        {
            continue;
        }

        ctrl = USB_GET_TX_CTRL(ep_idx);
        if(ch32_usbhs_in_may_resume(
               g_ch32_usbhs_udc.in_ep[ep_idx].ep_enabled,
               g_ch32_usbhs_udc.in_ep[ep_idx].ep_armed,
               g_ch32_usbhs_udc.in_ep[ep_idx].ep_stalled) &&
           ((ctrl & USBHS_EP_T_RES_MASK) != USBHS_EP_T_RES_STALL))
        {
            ctrl &= (uint8_t)~USBHS_EP_T_RES_MASK;
            ctrl |= USBHS_EP_T_RES_ACK;
            USB_SET_TX_CTRL(ep_idx, ctrl);
        }
    }
}

/** @brief Pause armed non-control IN endpoints for a new SETUP request. */
static void usb_dc_usbhs_pause_in_endpoints(void)
{
    uint8_t ctrl;
    uint8_t ep_idx;

    for(ep_idx = 1U; ep_idx < USB_NUM_BIDIR_ENDPOINTS; ep_idx++)
    {
        ctrl = USB_GET_TX_CTRL(ep_idx);
        if(g_ch32_usbhs_udc.in_ep[ep_idx].ep_enabled &&
           g_ch32_usbhs_udc.in_ep[ep_idx].ep_armed &&
           !g_ch32_usbhs_udc.in_ep[ep_idx].ep_stalled &&
           ((ctrl & USBHS_EP_T_RES_MASK) == USBHS_EP_T_RES_ACK))
        {
            ch32_usbhs_ep0_priority_defer_in(&ep0_priority_state, ep_idx);
            ctrl &= (uint8_t)~USBHS_EP_T_RES_MASK;
            ctrl |= USBHS_EP_T_RES_NAK;
            USB_SET_TX_CTRL(ep_idx, ctrl);
        }
    }
}

/** @brief Give a complete EP0 request priority over periodic IN traffic. */
static void usb_dc_usbhs_begin_control_priority(void)
{
    ch32_usbhs_ep0_priority_begin(
        &ep0_priority_state,
        (EP0_DatabufHD[0] & USB_REQUEST_DIR_MASK) ? 1U : 0U);
    ep0_priority_watchdog_started = 0U;
    usb_dc_usbhs_pause_in_endpoints();
}

/** @brief Release periodic IN traffic after the EP0 status transaction. */
static void usb_dc_usbhs_finish_control_priority(uint8_t token,
                                                  uint16_t transfer_length)
{
    uint16_t endpoint_mask;

    endpoint_mask = ch32_usbhs_ep0_priority_finish(
        &ep0_priority_state, token, transfer_length);
    if(!ep0_priority_state.active)
    {
        ep0_priority_watchdog_started = 0U;
    }
    usb_dc_usbhs_resume_in_endpoints(endpoint_mask);
}

/** @brief Release periodic IN traffic when the control request stalls. */
static void usb_dc_usbhs_release_stalled_control(void)
{
    uint8_t rx_ctrl = USBHS_DEVICE->UEP0_RX_CTRL;
    uint8_t tx_ctrl = USBHS_DEVICE->UEP0_TX_CTRL;

    if(ep0_priority_state.active &&
       (((rx_ctrl & USBHS_EP_R_RES_MASK) == USBHS_EP_R_RES_STALL) ||
        ((tx_ctrl & USBHS_EP_T_RES_MASK) == USBHS_EP_T_RES_STALL)))
    {
        usb_dc_usbhs_resume_in_endpoints(
            ch32_usbhs_ep0_priority_abort(&ep0_priority_state));
        ep0_priority_watchdog_started = 0U;
    }
}

/** @brief Arm or defer one non-control IN packet without racing EP0. */
static void usb_dc_usbhs_arm_non_control_in(uint8_t ep_idx,
                                            uint8_t data_toggle)
{
    uint8_t ctrl;
    uint8_t generation;

    do
    {
        generation = ep0_priority_state.generation;
        ctrl = USB_GET_TX_CTRL(ep_idx);
        ctrl &= (uint8_t)~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK);
        ctrl |= data_toggle ? USBHS_EP_T_TOG_1 : USBHS_EP_T_TOG_0;

        if(ep0_priority_state.active)
        {
            ch32_usbhs_ep0_priority_defer_in(&ep0_priority_state, ep_idx);
            ctrl |= USBHS_EP_T_RES_NAK;
        }
        else
        {
            ep0_priority_state.deferred_in_mask &=
                (uint16_t)~(uint16_t)(1UL << ep_idx);
            ctrl |= USBHS_EP_T_RES_ACK;
        }
        USB_SET_TX_CTRL(ep_idx, ctrl);
        __asm__ volatile("" ::: "memory");
    } while(generation != ep0_priority_state.generation);
}

__WEAK void usb_dc_low_level_init(void)
{
}

__WEAK void usb_dc_low_level_deinit(void)
{
}

int usb_dc_init(void)
{
    memset(&g_ch32_usbhs_udc, 0, sizeof(struct ch32_usbhs_udc));
    ch32_usbhs_ep0_priority_init(&ep0_priority_state);
    ep0_priority_watchdog_started = 0U;
    usbhs_suspended = 0U;

    usb_dc_low_level_init();

    /* Match WCH USBHS examples: clear endpoint/FIFO state and reset the SIE
     * before enabling the device pull-up. Some chips do not enumerate reliably
     * if the USBHS block is configured from an uncleared reset state. */
    USBHS_DEVICE->CONTROL = USBHS_ALL_CLR | USBHS_FORCE_RST;
    usb_dc_usbhs_sie_reset_delay();
    USBHS_DEVICE->CONTROL &= ~USBHS_FORCE_RST;

    USBHS_DEVICE->HOST_CTRL = 0x00;
    USBHS_DEVICE->HOST_CTRL = USBHS_PHY_SUSPENDM;

    USBHS_DEVICE->CONTROL = 0;
#ifdef CONFIG_USB_HS
    USBHS_DEVICE->CONTROL = USBHS_DMA_EN | USBHS_INT_BUSY_EN | USBHS_HIGH_SPEED;
#else
    USBHS_DEVICE->CONTROL = USBHS_DMA_EN | USBHS_INT_BUSY_EN | USBHS_FULL_SPEED;
#endif

    USBHS_DEVICE->INT_FG = 0xff;
    USBHS_DEVICE->INT_EN = 0;
    USBHS_DEVICE->INT_EN = USBHS_SETUP_ACT_EN | USBHS_TRANSFER_EN |
                            USBHS_DETECT_EN | USBHS_SUSPEND_EN;

    USBHS_DEVICE->ENDP_TYPE = 0x00;
    USBHS_DEVICE->BUF_MODE = 0x00;
    USBHS_DEVICE->DEV_AD = 0x00;

    USBHS_DEVICE->UEP0_DMA = (uint32_t)EP0_DatabufHD;
    usb_dc_usbhs_ep0_prepare();

    for (uint8_t ep_idx = 1; ep_idx < USB_NUM_BIDIR_ENDPOINTS; ep_idx++) {
        USB_SET_RX_DMA(ep_idx, (uint32_t)&g_ch32_usbhs_udc.ep_databuf[ep_idx - 1][0]);
        USB_SET_TX_DMA(ep_idx, (uint32_t)&g_ch32_usbhs_udc.ep_databuf[ep_idx - 1][512]);
    }

    USBHS_DEVICE->CONTROL |= USBHS_DEV_PU_EN;

    return 0;
}

int usb_dc_deinit(void)
{
    /* Present a real detach before resetting controller-owned endpoint state. */
    USBHS_DEVICE->CONTROL &= (uint8_t)~USBHS_DEV_PU_EN;
    USBHS_DEVICE->INT_EN = 0U;
    USBHS_DEVICE->ENDP_CONFIG = 0U;
    USBHS_DEVICE->INT_FG = 0xffU;
    USBHS_DEVICE->CONTROL |= USBHS_ALL_CLR | USBHS_FORCE_RST;
    ep0_tx_armed = 0U;
    mps_over_flag = 0U;
    ep0_priority_watchdog_started = 0U;
    usbhs_suspended = 0U;
    ch32_usbhs_ep0_priority_init(&ep0_priority_state);
    memset(&g_ch32_usbhs_udc, 0, sizeof(struct ch32_usbhs_udc));
    return 0;
}

int usbd_set_address(const uint8_t addr)
{
    if (addr == 0) {
        USBHS_DEVICE->DEV_AD = addr & 0xff;
    }
    g_ch32_usbhs_udc.dev_addr = addr;
    return 0;
}

int usbd_ep_open(const struct usbd_endpoint_cfg *ep_cfg)
{
    uint8_t ep_idx;
    struct ch32_usbhs_ep_state *ep_state;

    if(ep_cfg == NULL)
    {
        return -1;
    }
    ep_idx = USB_EP_GET_IDX(ep_cfg->ep_addr);
    if((ep_idx >= USB_NUM_BIDIR_ENDPOINTS) || (ep_cfg->ep_mps == 0U) ||
       (ep_cfg->ep_mps > 512U))
    {
        return -1;
    }

    if (USB_EP_DIR_IS_OUT(ep_cfg->ep_addr)) {
        ep_state = &g_ch32_usbhs_udc.out_ep[ep_idx];
        ep_state->ep_mps = ep_cfg->ep_mps;
        ep_state->ep_type = ep_cfg->ep_type;
        ep_state->ep_stalled = 0U;
        ep_state->ep_enabled = 1U;
        ep_state->ep_armed = 1U;
        USBHS_DEVICE->ENDP_CONFIG |= (1UL << (ep_idx + 16U));
        USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_0 | USBHS_EP_R_AUTOTOG);
    } else {
        ep_state = &g_ch32_usbhs_udc.in_ep[ep_idx];
        ep_state->ep_mps = ep_cfg->ep_mps;
        ep_state->ep_type = ep_cfg->ep_type;
        ep_state->ep_stalled = 0U;
        ep_state->ep_enabled = 1U;
        ep_state->ep_armed = 0U;
        USBHS_DEVICE->ENDP_CONFIG |= (1UL << ep_idx);
        USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0 | USBHS_EP_T_AUTOTOG);
        if(ep_idx != 0U)
        {
            epx_data_toggle[ep_idx - 1U] = false;
        }
    }
    USB_SET_MAX_LEN(ep_idx, ep_cfg->ep_mps);
    return 0;
}
int usbd_ep_close(const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    struct ch32_usbhs_ep_state *ep_state;

    if((ep_idx == 0U) || (ep_idx >= USB_NUM_BIDIR_ENDPOINTS))
    {
        return -1;
    }
    if(USB_EP_DIR_IS_OUT(ep))
    {
        ep_state = &g_ch32_usbhs_udc.out_ep[ep_idx];
        USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_AUTOTOG | USBHS_EP_R_RES_NAK);
        USBHS_DEVICE->ENDP_CONFIG &= ~(1UL << (ep_idx + 16U));
    }
    else
    {
        ep_state = &g_ch32_usbhs_udc.in_ep[ep_idx];
        USB_SET_TX_LEN(ep_idx, 0U);
        USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_AUTOTOG | USBHS_EP_T_RES_NAK);
        USBHS_DEVICE->ENDP_CONFIG &= ~(1UL << ep_idx);
        ep0_priority_state.deferred_in_mask &=
            (uint16_t)~(uint16_t)(1UL << ep_idx);
        epx_data_toggle[ep_idx - 1U] = false;
    }
    memset(ep_state, 0, sizeof(*ep_state));
    return 0;
}
int usbd_ep_set_stall(const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    struct ch32_usbhs_ep_state *ep_state;

    if(ep_idx >= USB_NUM_BIDIR_ENDPOINTS)
    {
        return -1;
    }

    if (USB_EP_DIR_IS_OUT(ep)) {
        ep_state = &g_ch32_usbhs_udc.out_ep[ep_idx];
        if(!ep_state->ep_enabled)
        {
            return -2;
        }
        if (ep_idx == 0) {
            USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_STALL;
        } else {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_STALL);
        }
    } else {
        ep_state = &g_ch32_usbhs_udc.in_ep[ep_idx];
        if(!ep_state->ep_enabled)
        {
            return -2;
        }
        if (ep_idx == 0) {
            USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_STALL;
        } else {
            USB_SET_TX_CTRL(ep_idx, (USB_GET_TX_CTRL(ep_idx) & ~USBHS_EP_T_RES_MASK) | USBHS_EP_T_RES_STALL);
        }
    }
    ep_state->ep_stalled = 1U;

    return 0;
}

int usbd_ep_clear_stall(const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    struct ch32_usbhs_ep_state *ep_state;

    if(ep_idx >= USB_NUM_BIDIR_ENDPOINTS)
    {
        return -1;
    }

    if (USB_EP_DIR_IS_OUT(ep)) {
        ep_state = &g_ch32_usbhs_udc.out_ep[ep_idx];
        if(!ep_state->ep_enabled)
        {
            return -2;
        }
        USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_0 |
                       (ep_idx ? USBHS_EP_R_AUTOTOG : 0U));
        ep_state->ep_armed = 1U;
    } else {
        ep_state = &g_ch32_usbhs_udc.in_ep[ep_idx];
        if(!ep_state->ep_enabled)
        {
            return -2;
        }
        USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0 |
                       (ep_idx ? USBHS_EP_T_AUTOTOG : 0U));
        ep_state->ep_armed = 0U;
        if(ep_idx != 0U)
        {
            epx_data_toggle[ep_idx - 1U] = false;
            ep0_priority_state.deferred_in_mask &=
                (uint16_t)~(uint16_t)(1UL << ep_idx);
        }
    }
    ep_state->ep_stalled = 0U;
    return 0;
}
int usbd_ep_is_stalled(const uint8_t ep, uint8_t *stalled)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    const struct ch32_usbhs_ep_state *ep_state;

    if((stalled == NULL) || (ep_idx >= USB_NUM_BIDIR_ENDPOINTS))
    {
        return -1;
    }
    ep_state = USB_EP_DIR_IS_OUT(ep) ?
                   &g_ch32_usbhs_udc.out_ep[ep_idx] :
                   &g_ch32_usbhs_udc.in_ep[ep_idx];
    if(!ep_state->ep_enabled)
    {
        return -2;
    }
    *stalled = ep_state->ep_stalled ? 1U : 0U;
    return 0;
}

int usbd_ep_write(const uint8_t ep, const uint8_t *data, uint32_t data_len, uint32_t *ret_bytes)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    struct ch32_usbhs_ep_state *ep_state;

    if((ep_idx >= USB_NUM_BIDIR_ENDPOINTS) || USB_EP_DIR_IS_OUT(ep))
    {
        return -1;
    }
    ep_state = &g_ch32_usbhs_udc.in_ep[ep_idx];
    if(!ep_state->ep_enabled || ep_state->ep_stalled)
    {
        return -3;
    }

    if (!data && data_len) {
        return -1;
    }

    if(ep_state->ep_armed && (ep_idx != 0U)) {
        if (ret_bytes) {
            *ret_bytes = 0;
        }
        return -2;
    }

    if (!data_len) {
        if (ep_idx == 0) {
            USB_SET_TX_LEN(ep_idx, 0);
            ep0_tx_armed = 1;
            ep_state->ep_armed = 1U;
        } else {
            USB_SET_TX_LEN(ep_idx, 0);
            ep_state->ep_armed = 1U;
            usb_dc_usbhs_arm_non_control_in(
                ep_idx, epx_data_toggle[ep_idx - 1] ? 1U : 0U);
            epx_data_toggle[ep_idx - 1] ^= 1;
        }
        return 0;
    }

    if (data_len >= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps) {
        data_len = g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
        if (ep_idx == 0) {
            mps_over_flag = 1;
        }
    }

    if (ep_idx == 0) {
        memcpy(&EP0_DatabufHD[0], data, data_len);
        USB_SET_TX_LEN(ep_idx, data_len);
        ep0_tx_armed = 1;
        ep_state->ep_armed = 1U;
    } else {
        USB_SET_TX_LEN(ep_idx, data_len);
        memcpy(&g_ch32_usbhs_udc.ep_databuf[ep_idx - 1][512], data, data_len);
        ep_state->ep_armed = 1U;
        usb_dc_usbhs_arm_non_control_in(
            ep_idx, epx_data_toggle[ep_idx - 1] ? 1U : 0U);
        epx_data_toggle[ep_idx - 1] ^= 1;
    }
    if (ret_bytes) {
        *ret_bytes = data_len;
    }

    return 0;
}

int usbd_ep_read(const uint8_t ep, uint8_t *data, uint32_t max_data_len, uint32_t *read_bytes)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint32_t read_count;
    struct ch32_usbhs_ep_state *ep_state;

    if((ep_idx >= USB_NUM_BIDIR_ENDPOINTS) || !USB_EP_DIR_IS_OUT(ep))
    {
        return -1;
    }
    ep_state = &g_ch32_usbhs_udc.out_ep[ep_idx];
    if(!ep_state->ep_enabled || ep_state->ep_stalled)
    {
        return -3;
    }

    if (!data && max_data_len) {
        return -1;
    }

    if (!max_data_len) {
        if (ep_idx) {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_ACK);
            ep_state->ep_armed = 1U;
        }
        return 0;
    }

    read_count = USBHS_DEVICE->RX_LEN;
    read_count = MIN(read_count, max_data_len);

    if (ep_idx == 0x00) {
        if ((max_data_len == 8) && !read_bytes) {
            read_count = 8;
            memcpy(data, &EP0_DatabufHD[0], 8);
        } else {
            memcpy(data, &EP0_DatabufHD[0], read_count);
        }
    } else {
        memcpy(data, &g_ch32_usbhs_udc.ep_databuf[ep_idx - 1][0], read_count);
    }

    if (read_bytes) {
        *read_bytes = read_count;
    }

    return 0;
}

/** @brief Mask controller interrupts for a bounded main-context operation. */
void usb_dc_usbhs_irq_lock(void)
{
    usbhs_saved_int_enable = USBHS_DEVICE->INT_EN;
    USBHS_DEVICE->INT_EN = 0U;
    __asm__ volatile("" ::: "memory");
}

/** @brief Restore controller interrupts after a bounded critical section. */
void usb_dc_usbhs_irq_unlock(void)
{
    __asm__ volatile("" ::: "memory");
    USBHS_DEVICE->INT_EN = usbhs_saved_int_enable;
}

/**
 * @brief Test whether an IN completion is already pending in hardware.
 *
 * @param ep_idx Numeric endpoint index.
 * @return 1 when the pending transfer flag belongs to this IN endpoint.
 */
static uint8_t usb_dc_usbhs_in_completion_pending(uint8_t ep_idx)
{
    uint8_t token;

    if((USBHS_DEVICE->INT_FG & USBHS_TRANSFER_FLAG) == 0U)
    {
        return 0U;
    }
    token = (uint8_t)(((USBHS_DEVICE->INT_ST & MASK_UIS_TOKEN) >> 4) &
                      0x03U);
    return ((token == PID_IN) &&
            ((USBHS_DEVICE->INT_ST & MASK_UIS_ENDP) == ep_idx)) ? 1U : 0U;
}

/**
 * @brief Recover a non-control IN endpoint without resetting its DATA PID.
 */
int usb_dc_usbhs_recover_in(uint8_t ep)
{
    uint8_t ctrl;
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    struct ch32_usbhs_ep_state *ep_state;

    if(USB_EP_DIR_IS_OUT(ep) || (ep_idx == 0U) ||
       (ep_idx >= USB_NUM_BIDIR_ENDPOINTS))
    {
        return -1;
    }

    usb_dc_usbhs_irq_lock();
    ep_state = &g_ch32_usbhs_udc.in_ep[ep_idx];
    if(usb_dc_usbhs_in_completion_pending(ep_idx))
    {
        usb_dc_usbhs_irq_unlock();
        return 0;
    }
    if(!ep_state->ep_enabled || ep_state->ep_stalled)
    {
        usb_dc_usbhs_irq_unlock();
        return -2;
    }

    /* NAK first, then recheck the SIE before releasing the software credit. */
    ctrl = USB_GET_TX_CTRL(ep_idx);
    ctrl &= (uint8_t)~USBHS_EP_T_RES_MASK;
    ctrl |= USBHS_EP_T_RES_NAK;
    USB_SET_TX_CTRL(ep_idx, ctrl);
    __asm__ volatile("" ::: "memory");
    if(usb_dc_usbhs_in_completion_pending(ep_idx))
    {
        usb_dc_usbhs_irq_unlock();
        return 0;
    }

    USB_SET_TX_LEN(ep_idx, 0U);
    ep_state->ep_armed = 0U;
    ep0_priority_state.deferred_in_mask &=
        (uint16_t)~(uint16_t)(1UL << ep_idx);
    usb_dc_usbhs_irq_unlock();
    return 1;
}

/**
 * @brief Release stale EP0 priority state after its bounded timeout.
 */
uint8_t usb_dc_usbhs_service(uint16_t now_ticks)
{
    uint16_t endpoint_mask;

    if(usbhs_suspended || !ep0_priority_state.active)
    {
        ep0_priority_watchdog_started = 0U;
        return 0U;
    }
    if(!ep0_priority_watchdog_started)
    {
        ep0_priority_deadline =
            (uint16_t)(now_ticks + USB_DC_USBHS_EP0_TIMEOUT_TICKS);
        ep0_priority_watchdog_started = 1U;
        return 0U;
    }
    if(!ch32_usbhs_deadline_due(now_ticks, ep0_priority_deadline))
    {
        return 0U;
    }

    usb_dc_usbhs_irq_lock();
    if(!ep0_priority_state.active)
    {
        ep0_priority_watchdog_started = 0U;
        usb_dc_usbhs_irq_unlock();
        return 0U;
    }

    /* Abandon only controller priority; the next SETUP restarts EP0 core state. */
    ep0_tx_armed = 0U;
    g_ch32_usbhs_udc.in_ep[0].ep_armed = 0U;
    USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_1;
    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_NAK | USBHS_EP_R_TOG_1;
    endpoint_mask = ch32_usbhs_ep0_priority_abort(&ep0_priority_state);
    ep0_priority_watchdog_started = 0U;
    usb_dc_usbhs_resume_in_endpoints(endpoint_mask);
    usb_dc_usbhs_irq_unlock();
    return 1U;
}

void USBD_IRQHandler(void)
{
    uint32_t ep_idx, token;
    uint32_t tmp;
    uint16_t transfer_length;
    uint8_t intflag = 0;

    intflag = USBHS_DEVICE->INT_FG;

    /* SETUP preempts periodic IN traffic; those reports may lose one poll. */
    if (intflag & USBHS_SETUP_FLAG) {
        ep0_tx_armed = 0;
        mps_over_flag = 0;
        g_ch32_usbhs_udc.in_ep[0].ep_stalled = 0U;
        g_ch32_usbhs_udc.in_ep[0].ep_armed = 0U;
        g_ch32_usbhs_udc.out_ep[0].ep_stalled = 0U;
        g_ch32_usbhs_udc.out_ep[0].ep_armed = 1U;
        USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_1;
        USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_NAK | USBHS_EP_R_TOG_1;
        usb_dc_usbhs_begin_control_priority();
        usbd_event_notify_handler(USBD_EVENT_SETUP_NOTIFY, NULL);
        usb_dc_usbhs_ep0_finish_callback();
        usb_dc_usbhs_release_stalled_control();
        USBHS_DEVICE->INT_FG = USBHS_SETUP_FLAG;
    }

    /* Re-sample after SETUP so one already-completed endpoint is not lost. */
    intflag = USBHS_DEVICE->INT_FG;
    if (intflag & USBHS_TRANSFER_FLAG) {
        ep_idx = (USBHS_DEVICE->INT_ST) & MASK_UIS_ENDP;
        token = (((USBHS_DEVICE->INT_ST) & MASK_UIS_TOKEN) >> 4) & 0x03;

        if (token == PID_IN) {
            if (ep_idx == 0x00) {
                transfer_length = USB_GET_TX_LEN(0U);
                ep0_tx_armed = 0;
                g_ch32_usbhs_udc.in_ep[0].ep_armed = 0U;
                usbd_event_notify_handler(USBD_EVENT_EP0_IN_NOTIFY, NULL);
                if (g_ch32_usbhs_udc.dev_addr > 0) {
                    USBHS_DEVICE->DEV_AD = g_ch32_usbhs_udc.dev_addr & 0xff;
                    g_ch32_usbhs_udc.dev_addr = 0;
                }
                if (ep0_tx_armed) {
                    mps_over_flag = 0;
                    /* Continue EP0 control IN when the core loaded another packet. */
                    tmp = USBHS_DEVICE->UEP0_TX_CTRL;
                    tmp ^= USBHS_EP_T_TOG_1;
                    tmp &= ~USBHS_EP_T_RES_MASK;
                    tmp |= USBHS_EP_T_RES_ACK;
                    USBHS_DEVICE->UEP0_TX_CTRL = tmp;
                } else {
                    USBHS_DEVICE->UEP0_TX_CTRL =
                        (USBHS_DEVICE->UEP0_TX_CTRL & ~USBHS_EP_T_RES_MASK) |
                        USBHS_EP_T_RES_NAK;
                    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_1;
                }
                usb_dc_usbhs_finish_control_priority(PID_IN, transfer_length);
            } else {
                g_ch32_usbhs_udc.in_ep[ep_idx].ep_armed = 0U;
                USB_SET_TX_CTRL(ep_idx, (USB_GET_TX_CTRL(ep_idx) & ~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK)) | USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0);
                ep0_priority_state.deferred_in_mask &=
                    (uint16_t)~(uint16_t)(1UL << ep_idx);
                usbd_event_notify_handler(USBD_EVENT_EP_IN_NOTIFY, (void *)(ep_idx | 0x80));
            }
        } else if (token == PID_OUT) {
            if (ep_idx == 0x00) {
                if (USBHS_DEVICE->INT_ST & USBHS_DEV_UIS_TOG_OK) {
                    transfer_length = USBHS_DEVICE->RX_LEN;
                    /* Advance DATA1/DATA0 only after the SIE accepted the packet. */
                    USBHS_DEVICE->UEP0_RX_CTRL =
                        ch32_usbhs_ep0_rx_advance_toggle(
                            USBHS_DEVICE->UEP0_RX_CTRL);
                    ep0_tx_armed = 0;
                    usbd_event_notify_handler(USBD_EVENT_EP0_OUT_NOTIFY, NULL);
                    /* Arm only the status packet prepared by the control core. */
                    usb_dc_usbhs_ep0_finish_callback();
                    usb_dc_usbhs_release_stalled_control();
                    usb_dc_usbhs_finish_control_priority(PID_OUT,
                                                         transfer_length);
                }
            } else {
                if (USBHS_DEVICE->INT_ST & USBHS_DEV_UIS_TOG_OK) {
                    g_ch32_usbhs_udc.out_ep[ep_idx].ep_armed = 0U;
                    USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_NAK);
                    usbd_event_notify_handler(USBD_EVENT_EP_OUT_NOTIFY, (void *)(ep_idx & 0x7f));
                }
            }
        }
        USBHS_DEVICE->INT_FG = USBHS_TRANSFER_FLAG;
    }

    /* Keep reset handling bounded, but do not mask it behind endpoint work. */
    intflag = USBHS_DEVICE->INT_FG;
    if (intflag & USBHS_DETECT_FLAG) {
        usb_dc_usbhs_ep0_prepare();
        ch32_usbhs_ep0_priority_init(&ep0_priority_state);
        ep0_priority_watchdog_started = 0U;
        usbhs_suspended = 0U;

        for (uint8_t ep_idx = 1; ep_idx < USB_NUM_BIDIR_ENDPOINTS; ep_idx++) {
            USB_SET_TX_LEN(ep_idx, 0);
            USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_AUTOTOG | USBHS_EP_T_RES_NAK); // autotog does not work
            USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_AUTOTOG | USBHS_EP_R_RES_NAK);
            epx_data_toggle[ep_idx - 1] = false;
            memset(&g_ch32_usbhs_udc.in_ep[ep_idx], 0,
                   sizeof(g_ch32_usbhs_udc.in_ep[ep_idx]));
            memset(&g_ch32_usbhs_udc.out_ep[ep_idx], 0,
                   sizeof(g_ch32_usbhs_udc.out_ep[ep_idx]));
        }

        usbd_event_notify_handler(USBD_EVENT_RESET, NULL);
        USBHS_DEVICE->INT_FG = USBHS_DETECT_FLAG;
    }

    intflag = USBHS_DEVICE->INT_FG;
    if(intflag & USBHS_SUSPEND_FLAG)
    {
        uint8_t suspended_now;
        uint8_t transition;

        USBHS_DEVICE->INT_FG = USBHS_SUSPEND_FLAG;
        suspended_now = (USBHS_DEVICE->MIS_ST & USBHS_SUSPEND) ? 1U : 0U;
        transition = ch32_usbhs_suspend_transition(usbhs_suspended,
                                                   suspended_now);
        if(transition == CH32_USBHS_SUSPEND_ENTER)
        {
            usbhs_suspended = 1U;
            ep0_priority_watchdog_started = 0U;
            usbd_event_notify_handler(USBD_EVENT_SUSPEND, NULL);
        }
        else if(transition == CH32_USBHS_SUSPEND_LEAVE)
        {
            usbhs_suspended = 0U;
            usbd_event_notify_handler(USBD_EVENT_RESUME, NULL);
        }
    }
}
