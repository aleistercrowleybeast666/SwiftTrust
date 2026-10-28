#ifndef ST_REFERENCE_H
#define ST_REFERENCE_H
#include "st_ads.h"
#include "st_cal.h"
#include "st_radio.h"
#include "st_link2.h"
/* 2026-10-29 CAD contract, not a hardware-test declaration. Default ADC gain1
 * bypass allows raw capture before a sensor profile is selected. 8.192MHz HSE
 * feeds both the 96MHz MCU PLL and PA8 MCO1 /2 (4.096MHz ADC CLK).
 * TIM5 timestamps use a separate 1MHz counter derived from the MCU bus clock. */
enum
{
    ST_REF_ADC_HZ = ST_ADC_CLOCK_HZ,
    ST_REF_ADC_OSR = ST_ADC_OSR,
    ST_REF_ADC_RATE_MILLIHZ = ST_ADC_RATE_MILLIHZ,
    ST_REF_BRIDGE_UV = ST_BRIDGE_EXCITATION_UV,
    ST_REF_ADC_GAIN0 = ST_ADC_GAIN_CH0,
    ST_REF_ADC_GAIN1 = ST_ADC_GAIN_CH1,
    ST_REF_UART_BAUD = ST_DEBUG_UART_BAUD
};
#define ST_REF_RADIO_HZ ST_RADIO_FREQUENCY_HZ
typedef struct
{
    st_cal_binding binding;
    st_cal_policy policy;
    st_ads_config adc_config;
    bool configured;
    bool auto_zero;
} st_ref_sensor;
st_ads_config st_ref_ads_config(bool reference_electrical_profile_accepted);
st_radio_config st_ref_radio_config(bool reference_gpio_spi_profile_accepted);
st2_timing st_ref_link_timing(void);
/* Shared project identity; built into both images, with no operator key entry.
 * Public source is not a secret: this separates accidental traffic, not attackers. */
const uint8_t *st_ref_ignition_key(void);
/* Shared application boundary, called only after ADS readback/rate/freshness,
 * mounted storage, supply and idle guards. Clock confirmation must precede the
 * calibration callback so the first START after boot applies saved coefficients.
 * Does not start capture, initialize hardware, or bypass a calibration wizard. */
bool st_ref_prepare_start(st_logger *, st2_cal_port, uint32_t epoch, bool source_confirmed,
                          bool sensor_configured);

/* Exact same reference clock/gains as logging; ACK_SENSOR_RATING is mandatory.
 * Unknown, duplicate, missing, overflowing keys or malformed lines reject all.
 * Defaults remain raw-only; this parser does not infer sensor identity/ratings. */
bool st_ref_sensor_parse(st_ref_sensor *, const char *, size_t);
bool st_ref_vdda_mv(uint16_t factory_code, uint32_t factory_mv, uint16_t raw, uint32_t *mv);
bool st_ref_rate_ok(uint32_t edges, uint64_t elapsed_us);
#endif
