#if defined(ST_REFERENCE_APPLICATION)
#include "stm32f4xx_hal.h"
#include "st_ignition_f4.h"

#define IGNITION_TIMER TIM2
#define IGNITION_PORT GPIOA
#define IGNITION_PIN GPIO_PIN_0
#define IGNITION_TIMER_HZ 1000000u
#define IGNITION_TIMER_COUNTS (IGNITION_PULSE_MS * 1000u)
static volatile bool pulse_ready, pulse_fault;

void IgnitionPulse_FaultIsr(void)
{
    pulse_fault = true;
    pulse_ready = false;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    IGNITION_PORT->BSRR = (uint32_t)IGNITION_PIN << 16;
    /* Disconnect the timer physically. If this NMI interrupted Start(), its
     * resumed timer writes cannot reassert PA0 after the clock has failed. */
    MODIFY_REG(IGNITION_PORT->MODER, GPIO_MODER_MODER0, GPIO_MODER_MODER0_0);
    IGNITION_TIMER->CCMR1 = TIM_OCMODE_FORCED_INACTIVE;
    IGNITION_TIMER->CR1 = TIM_CR1_OPM;
}

IgnitionOutputResult IgnitionPulse_Stop(void *ctx)
{
    (void)ctx;
    if (!pulse_ready || pulse_fault)
        return IGNITION_OUTPUT_INVALID;
    /* Force low before resetting CNT; PWM1 would return HIGH at an OPM update. */
    IGNITION_TIMER->CCMR1 = TIM_OCMODE_FORCED_INACTIVE;
    IGNITION_TIMER->CR1 = TIM_CR1_OPM;
    IGNITION_TIMER->CNT = 0;
    IGNITION_TIMER->EGR = TIM_EGR_UG;
    IGNITION_TIMER->SR = 0;
    return (IGNITION_TIMER->CR1 & TIM_CR1_CEN) ? IGNITION_OUTPUT_FAULT : IGNITION_OUTPUT_OK;
}
bool IgnitionPulse_IsRunning(void *ctx)
{
    (void)ctx;
    return pulse_ready && !pulse_fault && (IGNITION_TIMER->CR1 & TIM_CR1_CEN) != 0;
}
IgnitionOutputResult IgnitionPulse_Start(void *ctx)
{
    (void)ctx;
    if (!ST_OUTPUT_HARDWARE_ENABLED || !ST_OUTPUT_SAFETY_REVIEWED)
        return IGNITION_OUTPUT_INVALID;
    if (!pulse_ready || pulse_fault)
        return IGNITION_OUTPUT_INVALID;
    if (IgnitionPulse_IsRunning(NULL))
        return IGNITION_OUTPUT_BUSY;
    IGNITION_TIMER->CR1 = TIM_CR1_OPM;
    IGNITION_TIMER->CCMR1 = TIM_OCMODE_FORCED_INACTIVE;
    IGNITION_TIMER->CNT = 0;
    IGNITION_TIMER->ARR = IGNITION_TIMER_COUNTS;
    IGNITION_TIMER->CCR1 = 1;
    IGNITION_TIMER->EGR = TIM_EGR_UG;
    IGNITION_TIMER->SR = 0;
    /* PWM2: low at CNT=0, high from CNT=1 through ARR, low after update.
     * OPM clears CEN on that update. No foreground/IRQ is needed to end it. */
    IGNITION_TIMER->CCMR1 = TIM_OCMODE_PWM2;
    IGNITION_TIMER->CR1 = TIM_CR1_OPM | TIM_CR1_CEN;
    return IGNITION_OUTPUT_OK;
}
IgnitionOutputResult IgnitionPulse_Init(void)
{
    pulse_ready = false;
    if (pulse_fault)
        return IGNITION_OUTPUT_FAULT;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(IGNITION_PORT, IGNITION_PIN, GPIO_PIN_RESET);
    GPIO_InitTypeDef gpio = {.Pin = IGNITION_PIN,
                             .Mode = GPIO_MODE_OUTPUT_PP,
                             .Pull = GPIO_PULLDOWN,
                             .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_Init(IGNITION_PORT, &gpio);
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    RCC_ClkInitTypeDef clocks = {0};
    uint32_t latency = 0;
    HAL_RCC_GetClockConfig(&clocks, &latency);
    uint32_t timer_hz = HAL_RCC_GetPCLK1Freq();
    if (clocks.APB1CLKDivider != RCC_HCLK_DIV1)
        timer_hz *= 2;
    if (!timer_hz || timer_hz % IGNITION_TIMER_HZ)
        return IGNITION_OUTPUT_INVALID;
    IGNITION_TIMER->PSC = timer_hz / IGNITION_TIMER_HZ - 1u;
    IGNITION_TIMER->ARR = IGNITION_TIMER_COUNTS;
    IGNITION_TIMER->CCR1 = 1;
    IGNITION_TIMER->CCER = TIM_CCER_CC1E;
    pulse_ready = true;
    (void)IgnitionPulse_Stop(NULL);
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(IGNITION_PORT, &gpio);
    return IGNITION_OUTPUT_OK;
}
#endif
