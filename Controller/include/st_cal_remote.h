#ifndef ST_CAL_REMOTE_H
#define ST_CAL_REMOTE_H
#include "st_cal_endpoint.h"
typedef struct
{
    st2_client *link;
    void *ctx;
    uint32_t (*now_ms)(void *);
    uint32_t last_poll_ms, cache_epoch;
    st_cal_view cached;
    bool have_cache, awaiting_result;
} st_cal_remote;
void st_cal_remote_init(st_cal_remote *, st2_client *, void *, uint32_t (*now_ms)(void *));
/* Invoke in controller foreground only while the calibration menu is visible.
 * One query per heartbeat at most; actual TX/retry uses the existing RF owner. */
void st_cal_remote_step(st_cal_remote *, bool menu_visible);
st_cal_control_port st_cal_remote_port(st_cal_remote *);
#endif
