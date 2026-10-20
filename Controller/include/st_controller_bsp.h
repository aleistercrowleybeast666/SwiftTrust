#ifndef SWIFTTHRUST_CONTROLLER_BSP_H
#define SWIFTTHRUST_CONTROLLER_BSP_H
#include <stdbool.h>
#include <stdint.h>
#if defined(ST_TARGET_BUILD) || defined(__arm__) || defined(__thumb__) || defined(__ICCARM__) ||   \
    defined(__ARMCC_VERSION)
#error                                                                                             \
    "SwiftThrust controller UNBOUND: verify exact GP/N part, schematic and supply divider before target integration"
#endif
/* Event-loop contract. All reads may report unavailable; zero is not full. */
typedef struct
{
    uint32_t millivolts;
    uint32_t age_ms;
    bool present;
    bool valid;
} st_controller_voltage;
typedef enum
{
    ST_VALUE_UNKNOWN,
    ST_VALUE_STALE,
    ST_VALUE_LOW,
    ST_VALUE_MEASURED
} st_voltage_state;
/* UI policy needs configured threshold and freshness timeout; neither is fixed.
 * Display controller and acquisition supplies separately. Future independent
 * power is UNKNOWN without a real sensing circuit. Voltage is not battery SOH.
 * Link RSSI/SNR and peer freshness are independent of local battery readings. */
st_controller_voltage st_controller_bsp_local_voltage(void);
void st_controller_bsp_poll_events(void);
/* No launch command, output driver or hardware binding is defined. */
#endif
