#include "st_reference.h"
#include <limits.h>
#include <string.h>
_Static_assert(ST_RADIO_SF == 10 && ST_RADIO_BANDWIDTH_HZ == 812500 && ST_RADIO_CR_DENOMINATOR == 5,
               "update verified SX128x enum mapping with the physical configuration");
const uint8_t *st_ref_ignition_key(void)
{
    static const uint8_t key[16] = {0x97, 0x4d, 0xa8, 0x5c, 0x32, 0xf1, 0x6e, 0x0b,
                                    0xd2, 0x79, 0x43, 0xbc, 0x18, 0x65, 0xe0, 0xaf};
    return key;
}
st_ads_config st_ref_ads_config(bool accepted)
{
    return (st_ads_config){ST_REF_ADC_HZ, ST_REF_ADC_OSR, ST_REF_ADC_GAIN0, ST_REF_ADC_GAIN1, 2,
                           accepted};
}
st_radio_config st_ref_radio_config(bool accepted)
{
    return (st_radio_config){.frequency_hz = ST_REF_RADIO_HZ,
                             .power_dbm = ST_RADIO_POWER_DBM,
                             .sync_word = ST_RADIO_SYNC_WORD,
                             .modulation = {SX128X_LORA_RANGING_SF10, SX128X_LORA_RANGING_BW_800,
                                            SX128X_LORA_RANGING_CR_4_5},
                             .tx_timeout_ms = ST_RADIO_TX_TIMEOUT_MS,
                             .verified = accepted};
}
st2_timing st_ref_link_timing(void)
{
    return (st2_timing){ST_LINK_HEARTBEAT_MS, ST_LINK_MAX_AIRTIME_MS, ST_LINK_RETRY_MS,
                        ST_LINK_OPERATION_MS, ST_LINK_ATTEMPTS};
}
bool st_ref_vdda_mv(uint16_t cal, uint32_t factory, uint16_t raw, uint32_t *out)
{
    if (!out || !cal || cal >= 4095 || !raw || raw >= 4095 || (factory != 3000 && factory != 3300))
        return false;
    uint32_t mv = ((uint32_t)cal * factory + raw / 2u) / raw;
    if (mv < 1800 || mv > 3600)
        return false;
    *out = mv;
    return true;
}
bool st_ref_rate_ok(uint32_t edges, uint64_t elapsed)
{
    if (elapsed < 250000 || elapsed > 2000000 || edges > 5000)
        return false;
    uint64_t observed = (uint64_t)edges * 1000000u;
    /* Startup sanity check against nominal HSI clock, not clock metrology. */
    return observed >= 1950u * elapsed && observed <= 2050u * elapsed;
}
static bool number(const char *p, size_t n, uint32_t *out)
{
    if (!n)
        return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (p[i] < '0' || p[i] > '9')
            return false;
        unsigned d = (unsigned)(p[i] - '0');
        if (v > (UINT32_MAX - d) / 10u)
            return false;
        v = v * 10u + d;
    }
    *out = v;
    return true;
}
bool st_ref_sensor_parse(st_ref_sensor *out, const char *data, size_t n)
{
    if (!out)
        return false;
    *out = (st_ref_sensor){0};
    if (!data || !n || n > 1024)
        return false;
    static const char *const keys[] = {"ACK_SENSOR_RATING", "PROFILE_ID",       "SENSOR_ID",
                                       "WIRE_COUNT",        "RATED_FORCE_MN",   "EXCITATION_UV",
                                       "MAX_SPAN_COUNTS",   "MIN_DELTA_COUNTS", "AUTO_ZERO"};
    uint32_t values[9] = {0}, seen = 0;
    size_t at = 0;
    while (at < n)
    {
        size_t first = at;
        while (at < n && data[at] != '\n')
            at++;
        size_t end = at;
        if (at < n)
            at++;
        while (first < end && (data[first] == ' ' || data[first] == '\t' || data[first] == '\r'))
            first++;
        while (end > first &&
               (data[end - 1] == ' ' || data[end - 1] == '\t' || data[end - 1] == '\r'))
            end--;
        if (first == end || data[first] == '#')
            continue;
        if (end - first > 63)
            return false;
        size_t eq = first;
        while (eq < end && data[eq] != '=')
            eq++;
        if (eq == end)
            return false;
        unsigned key = 9;
        for (unsigned i = 0; i < 9; i++)
            if (strlen(keys[i]) == eq - first && !memcmp(keys[i], data + first, eq - first))
            {
                key = i;
                break;
            }
        if (key == 9 || (seen & (1u << key)) || !number(data + eq + 1, end - eq - 1, &values[key]))
            return false;
        seen |= 1u << key;
    }
    if ((seen & 255u) != 255u || values[0] != 1 || !values[1] || !values[2] ||
        (values[3] != 4 && values[3] != 6) || !values[4] || values[5] != ST_REF_BRIDGE_UV ||
        !values[6] || values[6] >= 16777215 || values[7] <= values[6] || values[7] > 16777215)
        return false;
    st_cal_binding b = {.profile_id = values[1],
                        .sensor_id = values[2],
                        .config_id = 20261020,
                        .adc_clock_hz = ST_REF_ADC_HZ,
                        .osr = ST_REF_ADC_OSR,
                        .rate_millihz = ST_REF_ADC_RATE_MILLIHZ,
                        .excitation_nominal_uv = values[5],
                        .filter_id = 1,
                        .rated_force_nn = (uint64_t)values[4] * 1000000u,
                        .gain0 = ST_REF_ADC_GAIN0,
                        .gain1 = ST_REF_ADC_GAIN1,
                        .wire_count = (uint8_t)values[3],
                        .power_mode = 2,
                        .verified = true,
                        .simulated = false};
    if (!st_cal_binding_valid(b))
        return false;
    if ((seen & 256u) && values[8] > 1)
        return false;
    out->auto_zero = (seen & 256u) ? values[8] != 0 : ST_AUTO_ZERO_DEFAULT != 0;
    out->binding = b;
    out->policy = (st_cal_policy){256, values[6], values[7], 100000, 5000000, 180000000};
    out->configured = true;
    return true;
}

bool st_ref_prepare_start(st_logger *l, st2_cal_port cal, uint32_t epoch, bool source_confirmed,
                          bool sensor_configured)
{
    if (!l || !source_confirmed || !epoch || l->state != ST_IDLE || l->inflight ||
        atomic_load(&l->capture_enabled) || (cal.locked && cal.locked(cal.ctx)))
        return false;
    /* The board has now confirmed the source. The real calibration callback
     * validates this flag before applying the saved coefficients to the logger. */
    l->config.boot_epoch = epoch;
    l->config.clock_verified = true;
    if (cal.prepare_start && !cal.prepare_start(cal.ctx))
        return false;
    if (!sensor_configured)
        l->config.calibration_valid = false;
    return true;
}
