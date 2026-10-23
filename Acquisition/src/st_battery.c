#include "st_battery.h"
#define BATTERY_MAX_VALID_MV 4400u
#define BATTERY_MIN_VALID_MV 500u
/* Configurable NCR18650B usable-capacity bands; 12.5% starts the last LED blink.
 * Voltage under a changing load/temperature remains an approximate gauge. */
static const st_voltage_policy battery_policy = {
    {500, ST_BATTERY_TWO_MV, ST_BATTERY_THREE_MV, ST_BATTERY_FOUR_MV},
    BATTERY_HYSTERESIS_MV,
    BATTERY_FRESH_MS,
    true};
void Battery_Invalidate(BatteryMonitor *battery)
{
    if (!battery)
        return;
    battery->voltage.valid = false;
    battery->initialized = battery->blinking = false;
}
void Battery_Update(BatteryMonitor *battery, uint16_t code, const st_voltage *vdda, uint64_t now)
{
    if (!battery)
        return;
    if (!vdda || !vdda->valid || !vdda->present || vdda->mv < 2700 || vdda->mv > 3600 ||
        now < vdda->time_us || now - vdda->time_us > BATTERY_FRESH_MS * UINT64_C(1000))
    {
        Battery_Invalidate(battery);
        return;
    }
    st_voltage measured = st_voltage_from_adc(code, vdda->mv, BATTERY_DIVIDER_NUMERATOR,
                                              BATTERY_DIVIDER_DENOMINATOR, now,
                                              ST_ACQ_BATTERY_PRESENT, ST_ACQ_BATTERY_VERIFIED);
    if (!measured.valid || measured.mv < BATTERY_MIN_VALID_MV ||
        measured.mv > BATTERY_MAX_VALID_MV || code == 4095)
    {
        Battery_Invalidate(battery);
        return;
    }
    /* 10Hz samples, 1/4 IIR: suppress RF/load flicker without a slow multi-second lag. */
    battery->filtered_mv =
        battery->initialized ? (battery->filtered_mv * 3u + measured.mv + 2u) / 4u : measured.mv;
    battery->initialized = true;
    measured.mv = battery->filtered_mv;
    battery->voltage = measured;
}
st_voltage_view Battery_Evaluate(BatteryMonitor *battery, uint64_t now)
{
    if (!battery)
        return (st_voltage_view){0};
    battery->view = st_voltage_evaluate(&battery_policy, &battery->voltage, now, battery->view);
    st_voltage_view display = battery->view;
    if (display.kind != ST_V_MEASURED && display.kind != ST_V_LOW)
    {
        battery->blinking = battery->initialized = false;
        return display;
    }
    if (battery->blinking)
        battery->blinking = display.mv < BATTERY_BLINK_MV + BATTERY_HYSTERESIS_MV;
    else
        battery->blinking = display.mv < BATTERY_BLINK_MV;
    if (battery->blinking)
    {
        display.tier = 1;
        display.kind = ST_V_LOW;
        display.led_mask = (now / (BATTERY_BLINK_HALF_MS * UINT64_C(1000))) % 2u ? 0 : 1;
    }
    return display;
}
