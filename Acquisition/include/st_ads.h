#ifndef __ST_ADS_H
#define __ST_ADS_H
#include "st_core.h"

enum
{
    ADS1220_COMMAND_POWERDOWN = 0x02,
    ADS1220_COMMAND_RESET = 0x06,
    ADS1220_COMMAND_START = 0x08,
    ADS1220_COMMAND_RDATA = 0x10,
    ADS1220_COMMAND_RREG = 0x20,
    ADS1220_COMMAND_WREG = 0x40,
    ADS1220_REGISTER_COUNT = 4,
    ADS1220_DATA_BYTES = 3,
    ADS1220_TRANSFER_BYTES = 5,
    ADS1220_MODE_NORMAL = 0,
    ADS1220_MODE_DUTY = 1,
    ADS1220_MODE_TURBO = 2,
    ADS1220_RATE_2000_TURBO = 6
};
typedef enum
{
    ADS1220_OK,
    ADS1220_BUSY,
    ADS1220_INVALID_CONFIG,
    ADS1220_INVALID_STATE,
    ADS1220_SPI_ERROR,
    ADS1220_READBACK_ERROR,
    ADS1220_TIMEOUT
} Ads1220Result;
typedef struct
{
    void *ctx;
    st_async_result (*submit)(void *, const uint8_t *, uint8_t *, size_t);
    st_async_result (*poll)(void *);
} st_ads_port;
typedef struct
{
    uint32_t clock_hz, reference_uv, avdd_uv;
    uint32_t max_differential_uv, common_mode_min_uv, common_mode_max_uv;
    uint8_t gain, data_rate, operating_mode;
    bool pga_bypass, continuous, input_range_known, electrically_verified;
} st_ads_config;
typedef struct
{
    st_ads_port port;
    st_ads_config config;
    uint8_t tx[ADS1220_TRANSFER_BYTES], rx[ADS1220_TRANSFER_BYTES];
    uint8_t values[ADS1220_REGISTER_COUNT];
    uint32_t due_us, started_us;
    unsigned phase;
    Ads1220Result result;
    bool inflight, ready, failed, powered_down;
} st_ads;
bool st_ads_config_valid(st_ads_config config);
Ads1220Result Ads1220_EncodeConfig(st_ads_config, uint8_t registers[ADS1220_REGISTER_COUNT]);
/* Exact external-clock periods from TI SBAS501D table 8-4, not nominal SPS. */
uint32_t Ads1220_GetConversionCycles(st_ads_config);
uint32_t Ads1220_GetRateMilliHz(st_ads_config);
uint32_t Ads1220_GetFirstConversionCycles(st_ads_config);
int32_t Ads1220_DecodeRaw24(const uint8_t raw[ADS1220_DATA_BYTES]);
void st_ads_init(st_ads *, st_ads_port, st_ads_config, uint32_t now_us);
/* Nonblocking command owner. MCO must be stable before init. SPI mode 1,
 * 8-bit/MSB; no reset GPIO, device ID, status word or ADC CRC exists. */
void st_ads_tick(st_ads *, uint32_t now_us);
/* Caller must quiesce the capture DMA and hand back exclusive SPI ownership.
 * POWERDOWN completes the conversion already in progress; tick waits for it.
 * Reinitialize repeats RESET, default readback, config write and readback. */
Ads1220Result Ads1220_RequestPowerdown(st_ads *, uint32_t now_us);
Ads1220Result Ads1220_Reinitialize(st_ads *, uint32_t now_us);
#endif
