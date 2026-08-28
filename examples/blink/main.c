#include <stdint.h>

#include "ch32v30x.h"

#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

#define PWM_PERIOD_TICKS 1000U
#define PWM_FREQUENCY_HZ 1000U
#define BREATH_STEP       4U
#define BREATH_STEP_MS    8U

static void delay_ms(uint32_t milliseconds)
{
    uint64_t ticks = ((uint64_t)SystemCoreClock * milliseconds) / 8000U;

    SysTick->SR &= ~(1U << 0);
    SysTick->CMP = ticks;
    SysTick->CTLR |= (1U << 4);
    SysTick->CTLR |= (1U << 5) | (1U << 0);

    while ((SysTick->SR & (1U << 0)) == 0U) {
    }

    SysTick->CTLR &= ~(1U << 0);
}

static void led_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OCInitTypeDef output_compare = {0};
    TIM_TimeBaseInitTypeDef time_base = {0};
    uint32_t timer_tick_hz = PWM_PERIOD_TICKS * PWM_FREQUENCY_HZ;
    uint32_t prescaler = SystemCoreClock / timer_tick_hz;

    if (prescaler == 0U) {
        prescaler = 1U;
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1,
                           ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_8;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    time_base.TIM_Period = PWM_PERIOD_TICKS - 1U;
    time_base.TIM_Prescaler = (uint16_t)(prescaler - 1U);
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &time_base);

#if LED_ACTIVE_LOW
    output_compare.TIM_OCMode = TIM_OCMode_PWM2;
#else
    output_compare.TIM_OCMode = TIM_OCMode_PWM1;
#endif
    output_compare.TIM_OutputState = TIM_OutputState_Enable;
    output_compare.TIM_Pulse = 0U;
    output_compare.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &output_compare);

    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
}

int main(void)
{
    uint16_t level = 0U;
    int16_t direction = BREATH_STEP;

    SystemCoreClockUpdate();
    led_pwm_init();

    while (1) {
        uint32_t corrected_level =
            ((uint32_t)level * level) / PWM_PERIOD_TICKS;

        TIM_SetCompare1(TIM1, (uint16_t)corrected_level);
        delay_ms(BREATH_STEP_MS);

        if ((direction > 0) && (level >= PWM_PERIOD_TICKS - BREATH_STEP)) {
            level = PWM_PERIOD_TICKS;
            direction = -(int16_t)BREATH_STEP;
        } else if ((direction < 0) && (level <= BREATH_STEP)) {
            level = 0U;
            direction = BREATH_STEP;
        } else {
            level = (uint16_t)((int32_t)level + direction);
        }
    }
}
