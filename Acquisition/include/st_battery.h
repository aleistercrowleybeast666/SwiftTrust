#ifndef __ST_BATTERY_H
#define __ST_BATTERY_H
#include "st_core.h"
enum
{
    BATTERY_DIVIDER_NUMERATOR = 2,
    BATTERY_DIVIDER_DENOMINATOR = 1,
    BATTERY_FRESH_MS = 1000,
    BATTERY_BLINK_MV = 3300,
    BATTERY_HYSTERESIS_MV = 30,
    BATTERY_BLINK_HALF_MS = 500
};
typedef struct
{
    st_voltage voltage;
    st_voltage_view view;
    uint32_t filtered_mv;
    bool initialized, blinking;
} BatteryMonitor;
/* PC0 ADC1_IN10: 10k from BAT+ to PC0, 10k PC0 to GND, 100nF PC0 to GND.
 * VREFINT provides actual VDDA; this is a voltage estimate, not a fuel gauge. */
void Battery_Update(BatteryMonitor *, uint16_t code, const st_voltage *vdda, uint64_t now_us);
void Battery_Invalidate(BatteryMonitor *);
st_voltage_view Battery_Evaluate(BatteryMonitor *, uint64_t now_us);
#endif
