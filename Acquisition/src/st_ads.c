#include "st_ads.h"
#include <string.h>

#define ADS1220_INIT_TIMEOUT_US 2000000u
#define ADS1220_POWER_SETTLE_US 1000u
#define ADS1220_RESET_SETTLE_US 50u
#define ADS1220_RESET_CLOCKS 32u
#define ADS1220_PGA_HEADROOM_UV 200000u
enum
{
    ADS1220_PHASE_RESET,
    ADS1220_PHASE_DEFAULTS,
    ADS1220_PHASE_WRITE,
    ADS1220_PHASE_START,
    ADS1220_PHASE_VERIFY,
    ADS1220_PHASE_READY,
    ADS1220_PHASE_POWERDOWN,
    ADS1220_PHASE_DRAIN,
    ADS1220_PHASE_OFF
};
static bool Ads1220_IsGainValid(uint8_t gain)
{
    return gain && gain <= 128u && (gain & (gain - 1u)) == 0;
}
bool st_ads_config_valid(st_ads_config c)
{
    if (!c.electrically_verified || c.clock_hz < 500000u || c.clock_hz > 4500000u ||
        c.avdd_uv < 2300000u || c.avdd_uv > 5500000u || c.reference_uv < 750000u ||
        c.reference_uv > c.avdd_uv || !Ads1220_IsGainValid(c.gain) || c.data_rate > 6u ||
        c.operating_mode > ADS1220_MODE_TURBO || (c.pga_bypass && c.gain > 4u) ||
        (!c.continuous && c.operating_mode == ADS1220_MODE_DUTY))
        return false;
    /* Unknown bridge: only the conservative gain-one/bypass raw profile is
     * accepted. It does not claim sensor compatibility or calibrated force. */
    if (!c.input_range_known)
        return c.pga_bypass && c.gain == 1u;
    if (!c.max_differential_uv || c.common_mode_min_uv > c.common_mode_max_uv ||
        c.common_mode_max_uv > c.avdd_uv ||
        (uint64_t)c.max_differential_uv * c.gain > c.reference_uv)
        return false;
    uint64_t differential = c.max_differential_uv;
    if (c.pga_bypass)
        return 2ull * c.common_mode_min_uv >= differential &&
               2ull * c.common_mode_max_uv + differential <= 2ull * c.avdd_uv;
    /* TI equations 12..14. Check the complete declared signal range using
     * doubled microvolts so odd-valued half ranges are not rounded away. */
    uint64_t amplified = differential * c.gain;
    return 4ull * c.common_mode_min_uv >= c.avdd_uv &&
           2ull * c.common_mode_min_uv >= 2ull * ADS1220_PGA_HEADROOM_UV + amplified &&
           2ull * c.common_mode_max_uv + amplified <=
               2ull * (c.avdd_uv - ADS1220_PGA_HEADROOM_UV);
}
Ads1220Result Ads1220_EncodeConfig(st_ads_config c, uint8_t registers[ADS1220_REGISTER_COUNT])
{
    if (!registers || !st_ads_config_valid(c))
        return ADS1220_INVALID_CONFIG;
    uint8_t gain = c.gain, code = 0;
    while (gain > 1u)
    {
        gain >>= 1;
        ++code;
    }
    registers[0] = (uint8_t)(0x30u | ((unsigned)code << 1) | (c.pga_bypass ? 1u : 0u));
    registers[1] = (uint8_t)(((unsigned)c.data_rate << 5) |
                            ((unsigned)c.operating_mode << 3) | (c.continuous ? 4u : 0u));
    registers[2] = 0x40u; /* Dedicated REFP0/REFN0; IDACs, low-side switch off. */
    registers[3] = 0;     /* IDAC pins disconnected; dedicated DRDY only. */
    return ADS1220_OK;
}
uint32_t Ads1220_GetConversionCycles(st_ads_config c)
{
    static const uint32_t continuous[3][7] = {
        {204768u, 91120u, 46128u, 23664u, 12464u, 6896u, 4144u},
        {823120u, 364560u, 184592u, 94736u, 49936u, 27664u, 16656u},
        {102384u, 45560u, 23064u, 11832u, 6232u, 3448u, 2072u}};
    static const uint32_t single[2][7] = {
        {204850u, 91218u, 46226u, 23762u, 12562u, 6994u, 4242u},
        {102434u, 45618u, 23122u, 11890u, 6290u, 3506u, 2130u}};
    if (c.data_rate > 6u || c.operating_mode > ADS1220_MODE_TURBO)
        return 0;
    if (c.continuous)
        return continuous[c.operating_mode][c.data_rate];
    if (c.operating_mode == ADS1220_MODE_DUTY)
        return 0;
    return single[c.operating_mode == ADS1220_MODE_TURBO ? 1u : 0u][c.data_rate];
}
uint32_t Ads1220_GetFirstConversionCycles(st_ads_config c)
{
    uint32_t cycles = Ads1220_GetConversionCycles(c);
    return !cycles || !c.continuous ? cycles :
        cycles + (c.operating_mode == ADS1220_MODE_TURBO ? 114u : 210u);
}
uint32_t Ads1220_GetRateMilliHz(st_ads_config c)
{
    uint32_t cycles = Ads1220_GetConversionCycles(c);
    return cycles ? (uint32_t)(((uint64_t)c.clock_hz * 1000u + cycles / 2u) / cycles) : 0;
}
int32_t Ads1220_DecodeRaw24(const uint8_t raw[ADS1220_DATA_BYTES])
{
    uint32_t value = ((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2];
    return (value & 0x800000u) ? (int32_t)(value & 0x7fffffu) - 8388608 : (int32_t)value;
}
static uint32_t Ads1220_ClocksToUs(const st_ads *a, uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000u + a->config.clock_hz - 1u) /
                      a->config.clock_hz);
}
static void Ads1220_SetFailure(st_ads *a, Ads1220Result result)
{
    a->result = result;
    a->ready = false;
    a->failed = true;
}
void st_ads_init(st_ads *a, st_ads_port port, st_ads_config cfg, uint32_t now)
{
    if (!a)
        return;
    memset(a, 0, sizeof(*a));
    a->port = port;
    a->config = cfg;
    a->started_us = now;
    a->due_us = now + ADS1220_POWER_SETTLE_US;
    if (!port.submit || !port.poll || Ads1220_EncodeConfig(cfg, a->values) != ADS1220_OK)
        Ads1220_SetFailure(a, ADS1220_INVALID_CONFIG);
}
void st_ads_tick(st_ads *a, uint32_t now)
{
    if (!a || a->failed || a->ready || a->powered_down)
        return;
    if ((uint32_t)(now - a->started_us) > ADS1220_INIT_TIMEOUT_US)
    {
        Ads1220_SetFailure(a, ADS1220_TIMEOUT);
        return;
    }
    if (a->inflight)
    {
        st_async_result result = a->port.poll(a->port.ctx);
        if (result == ST_IO_WAIT)
            return;
        a->inflight = false;
        if (result != ST_IO_DONE)
        {
            Ads1220_SetFailure(a, ADS1220_SPI_ERROR);
            return;
        }
        if (a->phase == ADS1220_PHASE_DEFAULTS || a->phase == ADS1220_PHASE_VERIFY)
            for (unsigned i = 0; i < ADS1220_REGISTER_COUNT; ++i)
                if (a->rx[i + 1u] !=
                    (a->phase == ADS1220_PHASE_DEFAULTS ? 0u : a->values[i]))
                {
                    Ads1220_SetFailure(a, ADS1220_READBACK_ERROR);
                    return;
                }
        if (a->phase == ADS1220_PHASE_RESET)
            a->due_us = now + ADS1220_RESET_SETTLE_US +
                        Ads1220_ClocksToUs(a, ADS1220_RESET_CLOCKS);
        else if (a->phase == ADS1220_PHASE_POWERDOWN)
            a->due_us = now + Ads1220_ClocksToUs(a, Ads1220_GetFirstConversionCycles(a->config));
        ++a->phase;
    }
    if ((uint32_t)(now - a->due_us) >= 0x80000000u)
        return;
    if (a->phase == ADS1220_PHASE_READY)
    {
        a->ready = true;
        a->result = ADS1220_OK;
        return;
    }
    if (a->phase == ADS1220_PHASE_DRAIN)
    {
        a->phase = ADS1220_PHASE_OFF;
        a->powered_down = true;
        a->result = ADS1220_OK;
        return;
    }
    memset(a->tx, 0, sizeof(a->tx));
    memset(a->rx, 0, sizeof(a->rx));
    size_t bytes = 1;
    switch (a->phase)
    {
    case ADS1220_PHASE_RESET:
        a->tx[0] = ADS1220_COMMAND_RESET;
        break;
    case ADS1220_PHASE_DEFAULTS:
    case ADS1220_PHASE_VERIFY:
        a->tx[0] = ADS1220_COMMAND_RREG | (ADS1220_REGISTER_COUNT - 1u);
        bytes = ADS1220_TRANSFER_BYTES;
        break;
    case ADS1220_PHASE_WRITE:
        a->tx[0] = ADS1220_COMMAND_WREG | (ADS1220_REGISTER_COUNT - 1u);
        memcpy(a->tx + 1, a->values, sizeof(a->values));
        bytes = ADS1220_TRANSFER_BYTES;
        break;
    case ADS1220_PHASE_START:
        a->tx[0] = ADS1220_COMMAND_START;
        break;
    case ADS1220_PHASE_POWERDOWN:
        a->tx[0] = ADS1220_COMMAND_POWERDOWN;
        break;
    default:
        Ads1220_SetFailure(a, ADS1220_INVALID_STATE);
        return;
    }
    st_async_result result = a->port.submit(a->port.ctx, a->tx, a->rx, bytes);
    if (result == ST_IO_DONE)
        a->inflight = true;
    else if (result != ST_IO_WAIT)
        Ads1220_SetFailure(a, ADS1220_SPI_ERROR);
}
Ads1220Result Ads1220_RequestPowerdown(st_ads *a, uint32_t now)
{
    if (!a || !a->port.submit || !a->port.poll || !st_ads_config_valid(a->config))
        return ADS1220_INVALID_CONFIG;
    if (a->inflight)
        return ADS1220_BUSY;
    if (a->powered_down)
        return ADS1220_OK;
    a->ready = false;
    a->failed = false;
    a->phase = ADS1220_PHASE_POWERDOWN;
    a->started_us = a->due_us = now;
    a->result = ADS1220_BUSY;
    return ADS1220_OK;
}
Ads1220Result Ads1220_Reinitialize(st_ads *a, uint32_t now)
{
    if (!a)
        return ADS1220_INVALID_CONFIG;
    if (a->inflight)
        return ADS1220_BUSY;
    if (!a->powered_down && !a->failed)
        return ADS1220_INVALID_STATE;
    st_ads_port port = a->port;
    st_ads_config config = a->config;
    st_ads_init(a, port, config, now);
    return a->result;
}
