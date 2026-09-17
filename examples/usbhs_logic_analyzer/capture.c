/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "capture.h"

#include "ch32v30x.h"
#include "ch32v30x_dma.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_misc.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_tim.h"

/* TIM2_UP is connected to DMA1 channel 2 on CH32V30x. The transfer source is
 * intentionally GPIOA->INDR rather than a TIM register: the timer request is
 * only the sampling strobe. This must be checked on each target silicon lot
 * before increasing CAPTURE_MAX_RATE_HZ. */
#define CAPTURE_DMA_CHANNEL DMA1_Channel2

static uint8_t sample_ring[CAPTURE_RING_BYTES] __attribute__((aligned(4)));
static volatile enum capture_state state;
static volatile uint32_t dma_wraps;
static volatile uint8_t immediate_complete;
static struct capture_result result;
static uint32_t requested_samples;
static uint32_t post_trigger_samples;
static uint32_t scan_sample;
static uint32_t trigger_sample;
static uint8_t trigger_mask;
static uint8_t trigger_value;
static uint8_t trigger_seen;
static uint8_t trigger_enabled;

void DMA1_Channel2_IRQHandler(void)
    __attribute__((interrupt("WCH-Interrupt-fast")));

static void capture_hardware_stop(void)
{
    TIM_DMACmd(TIM2, TIM_DMA_Update, DISABLE);
    TIM_Cmd(TIM2, DISABLE);
    DMA_Cmd(CAPTURE_DMA_CHANNEL, DISABLE);
}

void DMA1_Channel2_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TE2) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TE2 | DMA1_IT_GL2);
        capture_hardware_stop();
        state = CAPTURE_OVERFLOW;
        return;
    }

    if (DMA_GetITStatus(DMA1_IT_TC2) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TC2 | DMA1_IT_GL2);
        if (trigger_enabled != 0U) {
            dma_wraps++;
        } else {
            capture_hardware_stop();
            immediate_complete = 1U;
        }
    }
}

void capture_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                    GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);
    state = CAPTURE_IDLE;
}

static uint32_t capture_timer_configure(uint32_t requested_rate)
{
    TIM_TimeBaseInitTypeDef timer = {0};
    uint32_t ticks;
    uint32_t prescaler;
    uint32_t period;

    if (requested_rate == 0U || requested_rate > CAPTURE_MAX_RATE_HZ) {
        requested_rate = CAPTURE_MAX_RATE_HZ;
    }
    ticks = (SystemCoreClock + requested_rate / 2U) / requested_rate;
    if (ticks == 0U) {
        ticks = 1U;
    }
    prescaler = (ticks + 65535U) / 65536U;
    if (prescaler == 0U) {
        prescaler = 1U;
    }
    if (prescaler > 65536U) {
        prescaler = 65536U;
    }
    period = (ticks + prescaler / 2U) / prescaler;
    if (period == 0U) {
        period = 1U;
    }
    if (period > 65536U) {
        period = 65536U;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_DeInit(TIM2);
    timer.TIM_Prescaler = (uint16_t)(prescaler - 1U);
    timer.TIM_Period = (uint16_t)(period - 1U);
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    return SystemCoreClock / (prescaler * period);
}

static void capture_dma_configure(uint32_t count, uint8_t circular)
{
    DMA_InitTypeDef dma = {0};
    NVIC_InitTypeDef irq = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    DMA_DeInit(CAPTURE_DMA_CHANNEL);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&GPIOA->INDR;
    dma.DMA_MemoryBaseAddr = (uint32_t)sample_ring;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = count;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = circular ? DMA_Mode_Circular : DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(CAPTURE_DMA_CHANNEL, &dma);
    DMA_ClearITPendingBit(DMA1_IT_GL2);
    DMA_ITConfig(CAPTURE_DMA_CHANNEL, DMA_IT_TC | DMA_IT_TE, ENABLE);

    irq.NVIC_IRQChannel = DMA1_Channel2_IRQn;
    irq.NVIC_IRQChannelPreemptionPriority = 0U;
    irq.NVIC_IRQChannelSubPriority = 0U;
    irq.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&irq);
}

void capture_start(const struct sump_config *config)
{
    uint32_t rate;

    capture_abort();
    requested_samples = config->read_count;
    if (requested_samples < 4U) {
        requested_samples = 4U;
    }
    if (requested_samples > CAPTURE_ADVERTISED_BYTES) {
        requested_samples = CAPTURE_ADVERTISED_BYTES;
    }
    post_trigger_samples = config->delay_count;
    if (post_trigger_samples > requested_samples) {
        post_trigger_samples = requested_samples;
    }

    trigger_mask = (uint8_t)config->trigger_mask;
    trigger_value = (uint8_t)config->trigger_value;
    trigger_enabled = trigger_mask != 0U;
    trigger_seen = 0U;
    immediate_complete = 0U;
    dma_wraps = 0U;
    scan_sample = 0U;
    trigger_sample = 0U;
    result.first_sample = 0U;
    result.end_sample = 0U;
    result.sample_count = requested_samples;
    result.triggered = 0U;

    rate = sump_requested_rate_hz(config);
    result.actual_rate_hz = capture_timer_configure(rate);
    capture_dma_configure(trigger_enabled ? CAPTURE_RING_BYTES : requested_samples,
                          trigger_enabled);
    state = CAPTURE_ARMED;

    DMA_Cmd(CAPTURE_DMA_CHANNEL, ENABLE);
    TIM_DMACmd(TIM2, TIM_DMA_Update, ENABLE);
    TIM_SetCounter(TIM2, 0U);
    TIM_Cmd(TIM2, ENABLE);
}

void capture_abort(void)
{
    capture_hardware_stop();
    DMA_ClearITPendingBit(DMA1_IT_GL2);
    state = CAPTURE_IDLE;
    immediate_complete = 0U;
}

static uint32_t capture_produced_samples(void)
{
    uint32_t before;
    uint32_t after;
    uint16_t remaining;

    do {
        before = dma_wraps;
        remaining = DMA_GetCurrDataCounter(CAPTURE_DMA_CHANNEL);
        after = dma_wraps;
    } while (before != after);
    return after * CAPTURE_RING_BYTES +
           (CAPTURE_RING_BYTES - (uint32_t)remaining);
}

void capture_poll(void)
{
    uint32_t produced;
    uint32_t pre_trigger_samples;

    if (state != CAPTURE_ARMED) {
        return;
    }
    if (trigger_enabled == 0U) {
        if (immediate_complete != 0U) {
            result.first_sample = 0U;
            result.end_sample = requested_samples;
            state = CAPTURE_COMPLETE;
        }
        return;
    }

    produced = capture_produced_samples();
    if (produced - scan_sample > CAPTURE_RING_BYTES) {
        capture_hardware_stop();
        state = CAPTURE_OVERFLOW;
        return;
    }

    pre_trigger_samples = requested_samples - post_trigger_samples;
    if (pre_trigger_samples != 0U) {
        /* libsigrok marks the trigger after the pre-trigger region minus the
         * one trigger-stage sample. */
        pre_trigger_samples--;
    }
    while (scan_sample < produced && trigger_seen == 0U) {
        const uint8_t sample = sample_ring[scan_sample % CAPTURE_RING_BYTES];
        if (scan_sample >= pre_trigger_samples &&
            (sample & trigger_mask) == (trigger_value & trigger_mask)) {
            trigger_seen = 1U;
            trigger_sample = scan_sample;
            result.triggered = 1U;
            break;
        }
        scan_sample++;
    }

    if (trigger_seen != 0U) {
        const uint32_t capture_end =
            trigger_sample + 1U + post_trigger_samples;
        produced = capture_produced_samples();
        if (produced >= capture_end) {
            capture_hardware_stop();
            if (produced - capture_end >
                CAPTURE_RING_BYTES - CAPTURE_ADVERTISED_BYTES) {
                state = CAPTURE_OVERFLOW;
                return;
            }
            result.end_sample = capture_end;
            result.first_sample = capture_end - requested_samples;
            state = CAPTURE_COMPLETE;
        }
    }
}

enum capture_state capture_get_state(void)
{
    return state;
}

const struct capture_result *capture_get_result(void)
{
    return &result;
}

uint8_t capture_get_sample(uint32_t absolute_index)
{
    return sample_ring[absolute_index % CAPTURE_RING_BYTES];
}
