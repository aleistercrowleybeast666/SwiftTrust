#include "st_cal_system.h"
static uint64_t now(void *ctx)
{
    st_cal_system *s = ctx;
    return s->clock_us(s->clock_ctx);
}
static bool idle(void *ctx)
{
    st_cal_system *s = ctx;
    st_logger *l = s->disk.logger;
    return l && l->state == ST_IDLE && !l->inflight && !atomic_load(&l->capture_enabled) &&
           st_fatfs_idle(s->disk.worker);
}
st_cal_disk_state st_cal_system_init(st_cal_system *s, st_logger *l, st_fatfs_worker *w,
                                     st2_server *server, st_cal_binding binding,
                                     st_cal_policy policy, void *ctx, uint64_t (*clock)(void *))
{
    *s = (st_cal_system){.binding = binding, .clock_ctx = ctx, .clock_us = clock};
    st_cal_fatfs_init(&s->disk, w, l, binding);
    st_cal_disk_state state = st_cal_fatfs_open(&s->disk, &s->active);
    st_cal_storage storage = {0};
    if (state == ST_CAL_DISK_EMPTY || state == ST_CAL_DISK_VALID)
        storage = st_cal_fatfs_storage(&s->disk);
    st_cal_service_init(&s->service, &s->active, &s->binding, policy, storage, s,
                        clock ? now : NULL, idle);
    st_cal_endpoint_init(&s->endpoint, &s->service, l);
    if (server)
        st2_server_attach_calibration(server, st_cal_endpoint_port(&s->endpoint));
    if (l)
        (void)st_cal_apply_logger(&s->active, s->binding, l);
    return state;
}
