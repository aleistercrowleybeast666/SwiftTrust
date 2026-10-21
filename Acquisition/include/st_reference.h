#ifndef ST_REFERENCE_H
#define ST_REFERENCE_H
#include "st_ads.h"
#include "st_cal.h"
#include "st_radio.h"
#include "st_link2.h"
/* Executable REFERENCE design, not a frozen PCB or hardware-test declaration.
 * Default ADC PGA1 avoids assuming a load-cell sensitivity or high-gain CM range.
 * ADC CLKIN is an external 8.192MHz oscillator; no MCU pin drives it by default.
 * MCU timestamp scale is nominal HSI; its accuracy must be measured on a board. */
enum
{
    ST_REF_ADC_HZ = 8192000,
    ST_REF_ADC_OSR = 2048,
    ST_REF_ADC_RATE_MILLIHZ = 2000000,
    ST_REF_BRIDGE_UV = 2500000,
    ST_REF_ADC_GAIN0 = 1,
    ST_REF_ADC_GAIN1 = 1,
    ST_REF_UART_BAUD = 115200
};
#define ST_REF_RADIO_HZ UINT32_C(2473000000)
typedef struct
{
    st_cal_binding binding;
    st_cal_policy policy;
    bool configured;
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
