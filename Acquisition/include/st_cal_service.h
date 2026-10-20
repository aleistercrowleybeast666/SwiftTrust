#ifndef ST_CAL_SERVICE_H
#define ST_CAL_SERVICE_H
#include "st_cal.h"
typedef enum
{
    ST_CAL_ACTION_BEGIN,
    ST_CAL_ACTION_ZERO,
    ST_CAL_ACTION_REFERENCE,
    ST_CAL_ACTION_LOAD,
    ST_CAL_ACTION_SAVE,
    ST_CAL_ACTION_CANCEL,
    ST_CAL_ACTION_REFERENCE_CLEAR
} st_cal_action;
typedef struct
{
    st_cal_state state;
    st_cal_error error;
    uint32_t samples, required, revision, active_revision;
    int32_t zero_raw, loaded_raw;
    int64_t slope_nn, reference_force_nn;
    st_cal_reference reference;
    bool configured, storage_bound, idle, simulated, have_reference;
    bool pending, request_failed, uncertain;
} st_cal_view;
typedef struct
{
    void *ctx;
    bool (*request)(void *, st_cal_action, const st_cal_reference *);
    bool (*snapshot)(void *, st_cal_view *);
} st_cal_control_port;
typedef struct
{
    st_cal_workflow workflow;
    st_cal_profile *active;
    const st_cal_binding *current;
    st_cal_policy policy;
    st_cal_storage storage;
    void *ctx;
    uint64_t (*now_us)(void *);
    bool (*idle)(void *);
    st_cal_error last_error;
} st_cal_service;
/* Single serialized foreground owner: config/profile updates, sampling and
 * storage callbacks must not concurrently mutate this service.
 * Local acquisition-side/offline service. Its clock is the ADC capture clock.
 * A controller uses the paired-session compact RPC adapter, never a pointer
 * to acquisition RAM. Public UIDs and CRC do not provide hostile-radio authentication. */
void st_cal_service_init(st_cal_service *, st_cal_profile *, const st_cal_binding *, st_cal_policy,
                         st_cal_storage, void *, uint64_t (*now_us)(void *), bool (*idle)(void *));
st_cal_control_port st_cal_service_port(st_cal_service *);
bool st_cal_service_feed(st_cal_service *, const st_capture *);
void st_cal_service_source_error(st_cal_service *);
#endif
