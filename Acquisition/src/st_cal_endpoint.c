#include "st_cal_endpoint.h"
void st_cal_endpoint_init(st_cal_endpoint *e, st_cal_service *s, st_logger *l)
{
    *e = (st_cal_endpoint){.service = s, .logger = l, .last_ok = true};
    st_cal_input_init(&e->input);
}
void st_cal_endpoint_step(st_cal_endpoint *e)
{
    st_cal_view v;
    st_cal_control_port p = st_cal_service_port(e->service);
    (void)p.snapshot(p.ctx, &v);
    st_cal_input_step(&e->input, e->service);
}
static bool request(void *ctx, const uint8_t p[14])
{
    st_cal_endpoint *e = ctx;
    st_cal_reference r = {(st_cal_unit)p[1], st_get64(p + 2), st_get32(p + 10)};
    st_cal_control_port port = st_cal_service_port(e->service);
    e->last_ok =
        p[0] <= ST_CAL_ACTION_REFERENCE_CLEAR &&
        port.request(port.ctx, (st_cal_action)p[0], p[0] == ST_CAL_ACTION_REFERENCE ? &r : NULL);
    st_cal_endpoint_step(e);
    return e->last_ok;
}
static bool snapshot(void *ctx, uint8_t p[64])
{
    st_cal_endpoint *e = ctx;
    st_cal_view v;
    st_cal_control_port port = st_cal_service_port(e->service);
    if (!port.snapshot(port.ctx, &v))
        return false;
    v.request_failed = !e->last_ok;
    st_cal_view_encode(&v, p);
    return true;
}
static bool locked(void *ctx)
{
    st_cal_endpoint *e = ctx;
    st_cal_view v;
    st_cal_control_port p = st_cal_service_port(e->service);
    (void)p.snapshot(p.ctx, &v);
    return v.state >= ST_CAL_ZERO_READY && v.state <= ST_CAL_REVIEW;
}
static bool prepare(void *ctx)
{
    st_cal_endpoint *e = ctx;
    if (locked(e) || !e->service->current)
        return false;
    (void)st_cal_apply_logger(e->service->active, *e->service->current, e->logger);
    return e->logger->state == ST_IDLE && !e->logger->inflight &&
           !atomic_load(&e->logger->capture_enabled);
}
static void reset(void *ctx)
{
    st_cal_endpoint *e = ctx;
    st_cal_control_port p = st_cal_service_port(e->service);
    (void)p.request(p.ctx, ST_CAL_ACTION_CANCEL, NULL);
    e->last_ok = true;
    st_cal_endpoint_step(e);
}
st2_cal_port st_cal_endpoint_port(st_cal_endpoint *e)
{
    return (st2_cal_port){e, request, snapshot, locked, prepare, reset};
}
