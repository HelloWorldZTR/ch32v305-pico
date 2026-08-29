#include <stdint.h>

#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_tim.h"
#include "flash_disk.h"
#include "usbd_core.h"
#include "usbd_msc.h"

#define MSC_IN_EP  0x82
#define MSC_OUT_EP 0x03

#define USB_VID          0x1A86
#define USB_PID          0xFE13
#define USB_BCD_DEVICE   0x0101
#define USB_MAX_POWER    100
#define USB_LANGID_EN_US 0x0409
#define USB_CONFIG_SIZE  (9 + MSC_DESCRIPTOR_LEN)
#define HEARTBEAT_TIMER_HZ 1000000U
#define HEARTBEAT_HALF_PERIOD_US 250000U

static const uint8_t msc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00,
                               USB_VID, USB_PID, USB_BCD_DEVICE, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01,
                               USB_CONFIG_BUS_POWERED, USB_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, 0x00),

    USB_LANGID_INIT(USB_LANGID_EN_US),

    0x08, USB_DESCRIPTOR_TYPE_STRING,
    'W', 0, 'C', 0, 'H', 0,

    0x28, USB_DESCRIPTOR_TYPE_STRING,
    'C', 0, 'H', 0, '3', 0, '2', 0, 'V', 0, '3', 0, '0', 0, '5', 0,
    ' ', 0, 'U', 0, 'S', 0, 'B', 0, 'H', 0, 'S', 0, ' ', 0,
    'D', 0, 'i', 0, 's', 0, 'k', 0,

    0x12, USB_DESCRIPTOR_TYPE_STRING,
    '3', 0, '0', 0, '5', 0,
    'H', 0, 'S', 0,
    '0', 0, '1', 0, '0', 0,

    0x0A, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
    0x00
};

static void diagnostic_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_8;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(GPIOA, GPIO_Pin_8);
}

static void heartbeat_timer_init(void)
{
    TIM_TimeBaseInitTypeDef timer = {0};
    uint32_t divider;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    divider = SystemCoreClock / HEARTBEAT_TIMER_HZ;
    if (divider == 0U) {
        divider = 1U;
    }
    timer.TIM_Period = 0xFFFFU;
    timer.TIM_Prescaler = (uint16_t)(divider - 1U);
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_Cmd(TIM2, ENABLE);
}

void usbd_msc_get_cap(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;
    *block_num = FLASH_DISK_BLOCK_COUNT;
    *block_size = FLASH_DISK_BLOCK_SIZE;
}

int usbd_msc_sector_read(uint32_t sector, uint8_t *buffer, uint32_t length)
{
    return flash_disk_read(sector, buffer, length);
}

int usbd_msc_sector_write(uint32_t sector, uint8_t *buffer, uint32_t length)
{
    return flash_disk_write(sector, buffer, length);
}

void usb_dc_low_level_init(void)
{
    /* Keep USBHS independent of the optional external crystal. The 8 MHz HSI
     * is divided to the 4 MHz reference expected by the USBHS PHY PLL. */
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

int main(void)
{
    uint16_t now;
    uint16_t previous_tick;
    uint32_t heartbeat_elapsed = 0U;

    SystemCoreClockUpdate();
    diagnostic_led_init();
    heartbeat_timer_init();
    flash_disk_init();

    usbd_desc_register(msc_descriptor);
    usbd_msc_class_init(MSC_OUT_EP, MSC_IN_EP);
    usbd_initialize();
    previous_tick = TIM_GetCounter(TIM2);

    while (1) {
        now = TIM_GetCounter(TIM2);
        if (flash_disk_write_failed()) {
            GPIO_SetBits(GPIOA, GPIO_Pin_8);
            continue;
        }
        heartbeat_elapsed += (uint16_t)(now - previous_tick);
        previous_tick = now;
        if (heartbeat_elapsed >= HEARTBEAT_HALF_PERIOD_US) {
            heartbeat_elapsed -= HEARTBEAT_HALF_PERIOD_US;
            GPIOA->OUTDR ^= GPIO_Pin_8;
        }
    }
}
