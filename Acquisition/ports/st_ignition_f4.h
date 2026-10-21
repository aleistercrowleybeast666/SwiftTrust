#ifndef __ST_IGNITION_F4_H
#define __ST_IGNITION_F4_H
#include "st_ignition.h"
/* PA0 (LQFP64 pin 14), AF1 TIM2_CH1. Fixed 5s active-high logic signal. */
IgnitionOutputResult IgnitionPulse_Init(void);
IgnitionOutputResult IgnitionPulse_Start(void *);
IgnitionOutputResult IgnitionPulse_Stop(void *);
bool IgnitionPulse_IsRunning(void *);
#endif
