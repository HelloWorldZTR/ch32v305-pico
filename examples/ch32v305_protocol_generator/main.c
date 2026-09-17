/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include "ch32v30x.h"
#include "waveforms.h"

static uint16_t now_us(void)
{
    return (uint16_t)TIM_GetCounter(TIM2);
}

static void delay_us(uint16_t us)
{
    uint16_t start = now_us();
    while ((uint16_t)(now_us() - start) < us) {
    }
}

static void demo_write(enum demo_line line, uint8_t high)
{
    static const uint16_t pins[] = {
        GPIO_Pin_0, GPIO_Pin_1, GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_7
    };
    if (high) {
        GPIO_SetBits(GPIOA, pins[line]);
    } else {
        GPIO_ResetBits(GPIOA, pins[line]);
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    /* Establish idle levels before enabling the output drivers. */
    GPIO_SetBits(GPIOA, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_4);
    GPIO_ResetBits(GPIOA, GPIO_Pin_3 | GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_8);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_8;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_6;
    GPIO_Init(GPIOA, &gpio);
}

static void timers_init(void)
{
    TIM_TimeBaseInitTypeDef timer = {0};
    TIM_OCInitTypeDef pwm = {0};
    /* Shared 72 MHz HSE setup: APB1 = 36 MHz, TIM2/TIM3 = 72 MHz. */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
    timer.TIM_Prescaler = (uint16_t)(SystemCoreClock / 1000000U - 1U);
    timer.TIM_Period = 65535U;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_Cmd(TIM2, ENABLE);

    timer.TIM_Period = 999U;
    TIM_TimeBaseInit(TIM3, &timer);
    pwm.TIM_OCMode = TIM_OCMode_PWM1;
    pwm.TIM_OutputState = TIM_OutputState_Enable;
    pwm.TIM_Pulse = 375U;
    pwm.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM3, &pwm); /* PA6: 1 kHz, 37.5% duty cycle. */
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

static void uart_init(void)
{
    USART_InitTypeDef uart = {0};
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    uart.USART_BaudRate = 115200;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Tx; /* PA3 remains the pulse GPIO. */
    USART_Init(USART2, &uart);
    USART_Cmd(USART2, ENABLE);
}

int main(void)
{
    uint8_t counter = 0;
    uint16_t last;
    SystemCoreClockUpdate();
    gpio_init();
    /* Refuse to emit misleading timings if the expected clock is absent. */
    if (SystemCoreClock != 72000000U) {
        GPIO_SetBits(GPIOA, GPIO_Pin_8);
        while (1) {
        }
    }
    timers_init();
    uart_init();
    last = now_us();
    while (1) {
        if ((uint16_t)(now_us() - last) < 1000U) {
            continue;
        }
        last += 1000U;
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(USART2, counter);
        spi_demo_frame(counter);
        i2c_demo_frame(counter);
        GPIO_SetBits(GPIOA, GPIO_Pin_3);
        delay_us((counter & 0x1fU) + 2U);
        GPIO_ResetBits(GPIOA, GPIO_Pin_3);
        if ((counter & 0x7fU) == 0U) {
            GPIOA->OUTDR ^= GPIO_Pin_8;
        }
        counter++;
    }
}
