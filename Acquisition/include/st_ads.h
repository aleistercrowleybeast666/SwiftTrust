#ifndef ST_ADS_H
#define ST_ADS_H
#include "st_core.h"
typedef struct
{
    void *ctx;
    void (*reset)(void *, bool asserted);
    st_async_result (*submit)(void *, const uint8_t tx[12], uint8_t rx[12]);
    st_async_result (*poll)(void *);
} st_ads_port;
typedef struct
{
    uint32_t clock_hz;
    uint16_t osr;
    uint8_t gain0, gain1, power_mode;
    bool electrically_verified;
} st_ads_config;
typedef struct
{
    st_ads_port port;
    st_ads_config config;
    uint8_t tx[12], rx[12];
    uint16_t values[3];
    uint32_t due_us, started_us;
    unsigned phase;
    bool inflight, ready, failed;
} st_ads;
bool st_ads_config_valid(st_ads_config config);
void st_ads_init(st_ads *, st_ads_port, st_ads_config, uint32_t now_us);
/* Full-frame SPI, one submit per tick. Requires CLKIN running and stable power.
 * Ready only after reset acknowledgement, ID, write ACKs and register readbacks.
 * Acquisition SPI ownership must remain exclusive until ready. */
void st_ads_tick(st_ads *, uint32_t now_us);
#endif
