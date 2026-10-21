#include "stm32f4xx_hal.h"
#include "st_ignition_f4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
TIM_TypeDef pulse_model_timer;
GPIO_TypeDef pulse_model_gpio;
static unsigned checks, gpio_phase;
static uint32_t peripheral_hz=42000000u, divider=RCC_HCLK_DIV2;
#define C(x) do { checks++; if (!(x)) { fprintf(stderr,"pulse line%d: %s\n",__LINE__,#x);exit(1); } } while(0)
void PulseModel_Reset(void) { memset(&pulse_model_timer,0,sizeof pulse_model_timer); }
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint32_t pin,uint32_t state)
{ C(port==GPIOA&&pin==GPIO_PIN_0&&state==GPIO_PIN_RESET);gpio_phase=1; }
void HAL_GPIO_Init(GPIO_TypeDef *port,const GPIO_InitTypeDef *gpio)
{
    C(port==GPIOA&&gpio->Pin==GPIO_PIN_0&&gpio->Pull==GPIO_PULLDOWN);
    if(gpio->Mode==GPIO_MODE_OUTPUT_PP) { C(gpio_phase==1);gpio_phase=2; }
    else { C(gpio_phase==2&&gpio->Mode==GPIO_MODE_AF_PP&&gpio->Alternate==GPIO_AF1_TIM2);
        C(pulse_model_timer.CCMR1==TIM_OCMODE_FORCED_INACTIVE&&!(pulse_model_timer.CR1&TIM_CR1_CEN));gpio_phase=3; }
}
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *clocks,uint32_t *latency)
{ clocks->APB1CLKDivider=divider;*latency=2; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return peripheral_hz; }
static bool PulseModel_IsHigh(void)
{
    return pulse_model_timer.CCMR1==TIM_OCMODE_PWM2 &&
        (pulse_model_timer.CCER&TIM_CCER_CC1E) && pulse_model_timer.CNT>=pulse_model_timer.CCR1;
}
static void PulseModel_Tick(void)
{
    if(!(pulse_model_timer.CR1&TIM_CR1_CEN)) return;
    pulse_model_timer.CNT++;
    if(pulse_model_timer.CNT>pulse_model_timer.ARR)
    {
        pulse_model_timer.CNT=0;
        if(pulse_model_timer.CR1&TIM_CR1_OPM) pulse_model_timer.CR1 &= ~TIM_CR1_CEN;
    }
}
int main(void)
{
    C(IgnitionPulse_Start(NULL)==IGNITION_OUTPUT_INVALID);
    C(IgnitionPulse_Init()==IGNITION_OUTPUT_OK&&gpio_phase==3);
    C(pulse_model_timer.PSC==83&&pulse_model_timer.ARR==5000000&&pulse_model_timer.CCR1==1);
    C(!IgnitionPulse_IsRunning(NULL)&&!PulseModel_IsHigh());
    C(IgnitionPulse_Start(NULL)==IGNITION_OUTPUT_OK&&!PulseModel_IsHigh());
    C(IgnitionPulse_Start(NULL)==IGNITION_OUTPUT_BUSY);
    uint32_t high_us=0;
    for(uint32_t i=0;i<10000010;i++)
    {
        if(PulseModel_IsHigh()) high_us++;
        PulseModel_Tick();
    }
    C(high_us==5000000&&!IgnitionPulse_IsRunning(NULL)&&!PulseModel_IsHigh());
    C(pulse_model_timer.CNT==0); /* No repeat when the CPU does no service work. */
    C(IgnitionPulse_Start(NULL)==IGNITION_OUTPUT_OK);
    PulseModel_Tick();C(PulseModel_IsHigh());
    C(IgnitionPulse_Stop(NULL)==IGNITION_OUTPUT_OK&&!PulseModel_IsHigh());
    for(unsigned i=0;i<100;i++) PulseModel_Tick();
    C(!PulseModel_IsHigh());
    peripheral_hz=16000000;divider=RCC_HCLK_DIV1;
    C(IgnitionPulse_Init()==IGNITION_OUTPUT_OK&&pulse_model_timer.PSC==15);
    peripheral_hz=16000001;
    C(IgnitionPulse_Init()==IGNITION_OUTPUT_INVALID);
    C(IgnitionPulse_Start(NULL)==IGNITION_OUTPUT_INVALID&&!PulseModel_IsHigh());
    printf("PASS actual F4 pulse driver on register model: %u checks; 5,000,000 high ticks, reset-low, abort-low, clock divider, no foreground/IRQ cutoff dependency\n",checks);
    return 0;
}
