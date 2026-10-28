#ifndef __STM32F4XX_HAL_H
#define __STM32F4XX_HAL_H
/* Minimal register/clock facade for executing the REAL pulse driver on a host.
 * Register field values match CMSIS F411. This is not electrical validation. */
#include <stdint.h>
#include <stddef.h>
typedef struct { volatile uint32_t CR1, CCMR1, CNT, ARR, CCR1, EGR, SR, PSC, CCER; } TIM_TypeDef;
typedef struct { volatile uint32_t MODER, BSRR; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct { uint32_t APB1CLKDivider; } RCC_ClkInitTypeDef;
extern TIM_TypeDef pulse_model_timer;
extern GPIO_TypeDef pulse_model_gpio;
#define TIM2 (&pulse_model_timer)
#define GPIOA (&pulse_model_gpio)
#define TIM_CR1_CEN 0x01u
#define TIM_CR1_OPM 0x08u
#define TIM_EGR_UG 0x01u
#define TIM_CCER_CC1E 0x01u
#define TIM_OCMODE_FORCED_INACTIVE 0x40u
#define TIM_OCMODE_PWM2 0x70u
#define GPIO_PIN_0 0x01u
#define GPIO_PIN_RESET 0u
#define GPIO_MODE_OUTPUT_PP 0x01u
#define GPIO_MODE_AF_PP 0x02u
#define GPIO_PULLDOWN 0x02u
#define GPIO_SPEED_FREQ_LOW 0u
#define GPIO_AF1_TIM2 0x01u
#define GPIO_MODER_MODER0 0x03u
#define GPIO_MODER_MODER0_0 0x01u
#define MODIFY_REG(reg, clear, set) ((reg) = ((reg) & ~(clear)) | (set))
#define RCC_HCLK_DIV1 0u
#define RCC_HCLK_DIV2 0x1000u
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_FORCE_RESET() PulseModel_Reset()
#define __HAL_RCC_TIM2_RELEASE_RESET() ((void)0)
void PulseModel_Reset(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *,uint32_t,uint32_t);
void HAL_GPIO_Init(GPIO_TypeDef *,const GPIO_InitTypeDef *);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *,uint32_t *);
uint32_t HAL_RCC_GetPCLK1Freq(void);
#endif
