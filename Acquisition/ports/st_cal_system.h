#ifndef ST_CAL_SYSTEM_H
#define ST_CAL_SYSTEM_H
#include "st_cal_endpoint.h"
#include "st_cal_fatfs.h"
typedef struct
{
    st_cal_service service;
    st_cal_profile active;
    st_cal_binding binding;
    st_cal_endpoint endpoint;
    st_cal_fatfs disk;
    void *clock_ctx;
    uint64_t (*clock_us)(void *);
} st_cal_system;
/* Caller supplies the already verified ADS/sensor configuration, mounted volume,
 * initialized logger/worker, and the SAME extended DRDY microsecond clock.
 * This initializes calibration only, not clocks/GPIO/ADC/SD/radio/IRQ/tasks.
 * Call from the single foreground volume owner before accepting RF START.
 * Bind capture's calibration pointer to &system.endpoint.input before EXTI.
 * Feed the owner's loop with st_cal_endpoint_step(&system.endpoint).
 * Disk open failures leave calibration invalid/unbound, never invent defaults. */
st_cal_disk_state st_cal_system_init(st_cal_system *, st_logger *, st_fatfs_worker *, st2_server *,
                                     st_cal_binding, st_cal_policy, void *, uint64_t (*)(void *));
#endif
