#ifndef __ST_BATTERY_H
#define __ST_BATTERY_H
#include "st_core.h"
enum
{
    BATTERY_DIVIDER_NUMERATOR = ST_BATTERY_DIVIDER_NUMERATOR,
    BATTERY_DIVIDER_DENOMINATOR = ST_BATTERY_DIVIDER_DENOMINATOR,
    BATTERY_FRESH_MS = ST_BATTERY_FRESH_MS,
    BATTERY_BLINK_MV = ST_BATTERY_BLINK_MV,
    BATTERY_HYSTERESIS_MV = ST_BATTERY_HYSTERESIS_MV,
    BATTERY_BLINK_HALF_MS = ST_BATTERY_BLINK_HALF_MS
};
typedef struct
{
    st_voltage voltage;
    st_voltage_view view;
    uint32_t filtered_mv;
    bool initialized, blinking;
} BatteryMonitor;
/* PC0/F411 or PA3/G071: 10k from BAT+ to input, 10k || 100nF to GND.
 * VREFINT provides actual VDDA; this is a voltage estimate, not a fuel gauge.
 * Circuit acceptance is independent for the two boards. */
void Battery_UpdateCircuit(BatteryMonitor *, uint16_t code, const st_voltage *vdda,
                            uint64_t now_us, bool present, bool scale_verified);
/* Acquisition board's configured circuit. */
void Battery_Update(BatteryMonitor *, uint16_t code, const st_voltage *vdda, uint64_t now_us);
void Battery_Invalidate(BatteryMonitor *);
st_voltage_view Battery_Evaluate(BatteryMonitor *, uint64_t now_us);
#endif
