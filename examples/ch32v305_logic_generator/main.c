/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>

#include "ch32v30x.h"
#include "ch32v30x_dma.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_misc.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_tim.h"

#ifndef GENERATOR_RATE_HZ
#define GENERATOR_RATE_HZ 100000UL
#endif

#ifndef GENERATOR_PATTERN
#define GENERATOR_PATTERN 0
#endif

#define PATTERN_COUNTER     0
#define PATTERN_WALKING_ONE 1
#define PATTERN_ALTERNATING 2
#define PATTERN_LFSR        3
#define PATTERN_PULSE       4

#define PATTERN_SAMPLES 4096U

#if GENERATOR_RATE_HZ < 1 || GENERATOR_RATE_HZ > 8000000
#error "GENERATOR_RATE_HZ must be 1..8000000"
#endif

static uint32_t pattern[PATTERN_SAMPLES] __attribute__((aligned(4)));

void DMA1_Channel2_IRQHandler(void)
    __attribute__((interrupt("WCH-Interrupt-fast")));

static void pattern_build(void)
{
    uint32_t i;
    uint8_t value;
#if GENERATOR_PATTERN == PATTERN_LFSR
    uint8_t lfsr = 0xa5U;
#endif

    for (i = 0U; i < PATTERN_SAMPLES; i++) {
#if GENERATOR_PATTERN == PATTERN_COUNTER
        value = (uint8_t)i;
#elif GENERATOR_PATTERN == PATTERN_WALKING_ONE
        value = (uint8_t)(1U << (i & 7U));
#elif GENERATOR_PATTERN == PATTERN_ALTERNATING
        value = (i & 1U) != 0U ? 0xaaU : 0x55U;
#elif GENERATOR_PATTERN == PATTERN_LFSR
        value = lfsr;
        lfsr = (uint8_t)((lfsr << 1) |
                         (((lfsr >> 7) ^ (lfsr >> 5) ^
                           (lfsr >> 4) ^ (lfsr >> 3)) & 1U));
#elif GENERATOR_PATTERN == PATTERN_PULSE
        value = (i & 0xffU) < 16U ? 0xffU : 0x00U;
#else
#error "GENERATOR_PATTERN must be 0..4"
#endif
        /* Atomic set/reset leaves PA8 (user LED) and other pins untouched. */
        pattern[i] = (uint32_t)value | ((uint32_t)(value ^ 0xffU) << 16);
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                    GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOA, 0x00ffU);
    GPIO_ResetBits(GPIOB, GPIO_Pin_0);
}

static void timer_init(void)
{
    TIM_TimeBaseInitTypeDef timer = {0};
    uint32_t ticks;
    uint32_t prescaler;
    uint32_t period;

    ticks = (SystemCoreClock + GENERATOR_RATE_HZ / 2U) / GENERATOR_RATE_HZ;
    if (ticks == 0U) {
        ticks = 1U;
    }
    prescaler = (ticks + 65535U) / 65536U;
    if (prescaler == 0U) {
        prescaler = 1U;
    }
    period = (ticks + prescaler / 2U) / prescaler;
    if (period == 0U || period > 65536U || prescaler > 65536U) {
        while (1) {
        }
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    timer.TIM_Prescaler = (uint16_t)(prescaler - 1U);
    timer.TIM_Period = (uint16_t)(period - 1U);
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
}

static void dma_init(void)
{
    DMA_InitTypeDef dma = {0};
    NVIC_InitTypeDef irq = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    DMA_DeInit(DMA1_Channel2);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&GPIOA->BSHR;
    dma.DMA_MemoryBaseAddr = (uint32_t)pattern;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = PATTERN_SAMPLES;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel2, &dma);
    DMA_ClearITPendingBit(DMA1_IT_GL2);
    DMA_ITConfig(DMA1_Channel2, DMA_IT_TC | DMA_IT_TE, ENABLE);

    irq.NVIC_IRQChannel = DMA1_Channel2_IRQn;
    irq.NVIC_IRQChannelPreemptionPriority = 0U;
    irq.NVIC_IRQChannelSubPriority = 0U;
    irq.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&irq);
}

void DMA1_Channel2_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TE2) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TE2 | DMA1_IT_GL2);
        TIM_Cmd(TIM2, DISABLE);
        DMA_Cmd(DMA1_Channel2, DISABLE);
        GPIO_SetBits(GPIOB, GPIO_Pin_0);
        return;
    }
    if (DMA_GetITStatus(DMA1_IT_TC2) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TC2 | DMA1_IT_GL2);
        GPIOB->OUTDR ^= GPIO_Pin_0;
    }
}

int main(void)
{
    SystemCoreClockUpdate();
    pattern_build();
    gpio_init();
    timer_init();
    dma_init();

    DMA_Cmd(DMA1_Channel2, ENABLE);
    TIM_DMACmd(TIM2, TIM_DMA_Update, ENABLE);
    TIM_SetCounter(TIM2, 0U);
    TIM_Cmd(TIM2, ENABLE);
    while (1) {
    }
}
