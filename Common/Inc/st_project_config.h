#ifndef __ST_PROJECT_CONFIG_H
#define __ST_PROJECT_CONFIG_H
#include <stdint.h>
/* Same Pair ID on one pair. Change for another set; never derive identity from ADC. */
#define ST_PROJECT_PAIR_ID UINT32_C(0x53540001)
#define ST_PROTOCOL_VERSION 8u
#define ST_PROTOCOL_MARKER 0xD8u
#define ST_PROTOCOL_HEADER_BYTES 16u
#define ST_PROTOCOL_MAX_BYTES 64u
#ifndef ST_UI_CHINESE
#define ST_UI_CHINESE 1
#endif
#define ST_OLED_WIDTH 128u
#define ST_OLED_HEIGHT 64u
#define ST_OLED_ROWS 4u
#define ST_OLED_ROW_HEIGHT 16u
#define ST_UI_ROW_BYTES 64u
#define ST_BUTTON_DEBOUNCE_MS 30u
#define ST_BUTTON_LONG_MS 800u
#define ST_ENCODER_EDGES_PER_STEP 4u
#define ST_RADIO_FREQUENCY_HZ UINT32_C(2473000000)
#define ST_RADIO_POWER_DBM 12
#define ST_RADIO_SF 10u
#define ST_RADIO_BANDWIDTH_HZ 812500u
#define ST_RADIO_CR_DENOMINATOR 5u
#define ST_RADIO_SYNC_WORD 0x12u
#define ST_RADIO_PREAMBLE_SYMBOLS 16u
#define ST_RADIO_CRC_ENABLED 1
#define ST_RADIO_HIGH_SENSITIVITY_LNA 1
#define ST_RADIO_AGC_ENABLED 1
#define ST_RADIO_TX_TIMEOUT_MS 500u
#define ST_LINK_HEARTBEAT_MS 2000u
#define ST_LINK_SLOW_MS 4000u
#define ST_LINK_LOST_MS 7000u
#define ST_LINK_RETRY_MS 500u
#define ST_LINK_OPERATION_MS 120000u
#define ST_LINK_MAX_AIRTIME_MS 150u
#define ST_LINK_ATTEMPTS 4u
#define ST_LINK_RECONNECT_MS 2000u
#define ST_LINK_LED_SLOW_HALF_MS 500u
#define ST_LINK_LED_FAST_HALF_MS 125u
#define ST_VDDA_MIN_MV 2700u
#define ST_VDDA_MAX_MV 3600u
#define ST_VDDA_FRESH_MS 1000u
#define ST_VDDA_HYSTERESIS_MV 20u
#define ST_VDDA_DISPLAY_LEVELS_MV {3000u, 3100u, 3200u, 3300u}
#define ST_ADC_FRESH_US 10000u
#define ST_ADC_RATE_WINDOW_US 500000u
#define ST_DEBUG_UART_BAUD 115200u
#define ST_ADC_CLOCK_HZ 4096000u
/* ADS1220 Turbo DR=110: Table 8-4, 2072 CLK periods per continuous result. */
#define ST_ADC_OSR 0u /* Legacy field is inapplicable; explicit ADC1 metadata identifies format. */
#define ST_ADC_CONVERSION_CYCLES 2072u
#define ST_ADC_NOMINAL_RATE_MILLIHZ 2000000u
#define ST_ADC_RATE_MILLIHZ 1976834u
#define ST_ADC_REFERENCE_UV 5000000u
#define ST_ADC_GAIN_CH0 1u
#define ST_ADC_GAIN_CH1 0u /* No second differential channel is wired. */
#define ST_BRIDGE_EXCITATION_UV 5000000u
#define ST_CTRL_BATTERY_PRESENT 1
#define ST_CTRL_BATTERY_VERIFIED 1 /* Accepted CAD contract; bench measurement still required. */
#define ST_TRANSACTION_LIMIT UINT32_MAX
#define ST_AUTO_ZERO_DEFAULT 0
#define ST_ZERO_WINDOW_SAMPLES 512u
#define ST_ZERO_MAX_SPAN_COUNTS 2000u
#define ST_ZERO_TIMEOUT_MS 5000u
/* Saved acquisition netlist confirms switched BAT+ -> R19 10k -> PC0,
 * PC0 -> R20 10k || C19 100nF -> GND. VERIFIED accepts this CAD pin contract;
 * divider tolerance and ADC accuracy have not been measured on hardware. */
#ifndef ST_ACQ_BATTERY_PRESENT
#define ST_ACQ_BATTERY_PRESENT 1
#endif
#ifndef ST_ACQ_BATTERY_VERIFIED
#define ST_ACQ_BATTERY_VERIFIED 1
#endif
#define ST_BATTERY_DIVIDER_NUMERATOR 2u
#define ST_BATTERY_DIVIDER_DENOMINATOR 1u
#define ST_BATTERY_FRESH_MS 1000u
/* NCR18650B, 25C/650mA graph, approximate remaining usable capacity:
 * 75/50/25/12.5%, normalized from full to 3.2V (~3030mAh discharged).
 * Loaded-voltage estimate only; change these when the cell/load is selected.
 * The 3.2V reference endpoint does not implement automatic shutdown. */
#define ST_BATTERY_FOUR_MV 3840u
#define ST_BATTERY_THREE_MV 3630u
#define ST_BATTERY_TWO_MV 3480u
#define ST_BATTERY_BLINK_MV 3380u
#define ST_BATTERY_HYSTERESIS_MV 30u
#define ST_BATTERY_BLINK_HALF_MS 500u
#define ST_OUTPUT_ARM_MS 10000u
/* User-selected resistive load, nominal 1A / at most 2A, one 1s pulse.
 * REVIEWED accepts this configuration/code/CAD review, not a bench qualification.
 * ARM/FIRE confirmation, local interlocks and TIM2 autonomous cutoff remain. */
#define ST_OUTPUT_SIMULATION_MS 1000u
#define ST_OUTPUT_HARDWARE_ENABLED 1
#define ST_OUTPUT_SAFETY_REVIEWED 1
#define ST_OUTPUT_REAL_DURATION_MS 1000u
#define ST_OUTPUT_PROFILE_ID 1u
_Static_assert(ST_PROJECT_PAIR_ID != 0, "Pair ID must be configured");
_Static_assert(ST_PROTOCOL_VERSION == 8u && ST_PROTOCOL_HEADER_BYTES == 16u &&
                   ST_PROTOCOL_MAX_BYTES == 64u,
               "R8 frame layout consistency");
_Static_assert(ST_RADIO_PREAMBLE_SYMBOLS == 16u && ST_RADIO_CRC_ENABLED &&
                   ST_RADIO_HIGH_SENSITIVITY_LNA && ST_RADIO_AGC_ENABLED,
               "verified radio mapping");
_Static_assert(ST_OLED_ROWS *ST_OLED_ROW_HEIGHT == ST_OLED_HEIGHT, "four fixed OLED rows");
_Static_assert(ST_OLED_ROWS == 4u && ST_OLED_ROW_HEIGHT == 16u && ST_OLED_WIDTH == 128u,
               "R8 OLED layout is four rows of 16 pixels");
_Static_assert(ST_BUTTON_LONG_MS > ST_BUTTON_DEBOUNCE_MS, "button timing");
_Static_assert(ST_LINK_HEARTBEAT_MS < ST_LINK_SLOW_MS && ST_LINK_SLOW_MS < ST_LINK_LOST_MS,
               "link age thresholds");
_Static_assert(!ST_OUTPUT_HARDWARE_ENABLED ||
                   (ST_OUTPUT_SAFETY_REVIEWED && ST_OUTPUT_REAL_DURATION_MS > 0 &&
                    ST_OUTPUT_REAL_DURATION_MS <= 60000u),
               "real output requires reviewed duration");
#endif
