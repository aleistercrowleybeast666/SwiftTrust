#include "st_cal_endpoint.h"
#include <string.h>
static bool Zero_IsActive(void *ctx)
{
    return ((st_cal_endpoint *)ctx)->zero.state == ST_ZERO_COLLECTING;
}
static void Zero_Feed(void *ctx, const st_capture *capture)
{
    SessionZero_Feed(&((st_cal_endpoint *)ctx)->zero, capture);
}
static void Zero_Error(void *ctx)
{
    ((st_cal_endpoint *)ctx)->zero.state = ST_ZERO_FAILED;
}
void st_cal_endpoint_init(st_cal_endpoint *e, st_cal_service *s, st_logger *l)
{
    *e = (st_cal_endpoint){.service = s, .logger = l, .last_ok = true};
    st_cal_input_init(&e->input);
    e->auto_zero = ST_AUTO_ZERO_DEFAULT != 0;
    e->input.observer_ctx = e;
    e->input.observer_active = Zero_IsActive;
    e->input.observer_feed = Zero_Feed;
    e->input.observer_error = Zero_Error;
}
void st_cal_endpoint_step(st_cal_endpoint *e)
{
    st_cal_view v;
    st_cal_control_port p = st_cal_service_port(e->service);
    (void)p.snapshot(p.ctx, &v);
    st_cal_input_step(&e->input, e->service);
    if (e->service->now_us)
        SessionZero_Tick(&e->zero, e->service->now_us(e->service->ctx));
}
static bool request(void *ctx, const uint8_t p[14])
{
    st_cal_endpoint *e = ctx;
    if (p[0] >= 7)
    {
        if (e->logger->state != ST_IDLE || e->logger->inflight ||
            (e->service->workflow.state >= ST_CAL_ZERO_READY &&
             e->service->workflow.state <= ST_CAL_REVIEW))
            return false;
        if (p[0] == 8 && p[1] <= 1)
        {
            e->auto_zero = p[1] != 0;
            return true;
        }
        if (p[0] != 7 || !e->service->now_us)
            return false;
        e->last_ok = SessionZero_Begin(&e->zero, true, e->service->now_us(e->service->ctx),
                                       e->service->policy.max_span_counts
                                           ? e->service->policy.max_span_counts
                                           : ST_ZERO_MAX_SPAN_COUNTS) == ST_ZERO_OK;
        st_cal_endpoint_step(e);
        return e->last_ok;
    }
    if (e->zero.state == ST_ZERO_COLLECTING)
        return false;
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
    if (locked(e))
        return false;
    if (e->service->current)
        (void)st_cal_apply_logger(e->service->active, *e->service->current, e->logger);
    if (!e->start_prepared)
    {
        if (e->logger->config.session_zero_valid)
            e->logger->config.zero_raw = e->logger->config.calibration_zero_raw;
        e->logger->config.session_zero_valid = false;
        e->start_prepared = true;
        if (e->auto_zero)
        {
            e->zero.state = ST_ZERO_IDLE;
            if (!e->service->now_us ||
                SessionZero_Begin(&e->zero, true, e->service->now_us(e->service->ctx),
                                  e->service->policy.max_span_counts
                                      ? e->service->policy.max_span_counts
                                      : ST_ZERO_MAX_SPAN_COUNTS) != ST_ZERO_OK)
                return false;
            st_cal_endpoint_step(e);
        }
    }
    if (e->auto_zero && e->zero.state != ST_ZERO_READY)
        return false;
    if (e->zero.state == ST_ZERO_READY && SessionZero_Apply(&e->zero, e->logger) != ST_ZERO_OK)
        return false;
    return e->logger->state == ST_IDLE && !e->logger->inflight &&
           !atomic_load(&e->logger->capture_enabled);
}
static void reset(void *ctx)
{
    st_cal_endpoint *e = ctx;
    e->start_prepared = false;
    e->zero.state = ST_ZERO_IDLE;
    st_cal_control_port p = st_cal_service_port(e->service);
    (void)p.request(p.ctx, ST_CAL_ACTION_CANCEL, NULL);
    e->last_ok = true;
    st_cal_endpoint_step(e);
}
static bool Zero_IsWaiting(void *ctx)
{
    return ((st_cal_endpoint *)ctx)->zero.state == ST_ZERO_COLLECTING;
}
static bool Zero_HasFailed(void *ctx)
{
    return ((st_cal_endpoint *)ctx)->zero.state == ST_ZERO_FAILED;
}
static void Zero_BeginStart(void *ctx)
{
    ((st_cal_endpoint *)ctx)->start_prepared = false;
}
static bool Zero_Snapshot(void *ctx, uint8_t out[12])
{
    st_cal_endpoint *e = ctx;
    memset(out, 0, 12);
    out[0] = (uint8_t)e->zero.state;
    out[1] = e->auto_zero ? 1 : 0;
    out[2] = e->zero.state == ST_ZERO_READY ? 1 : 0;
    st_put32(out + 4, (uint32_t)e->zero.mean);
    st_put32(out + 8, e->zero.count);
    return true;
}
st2_cal_port st_cal_endpoint_port(st_cal_endpoint *e)
{
    return (st2_cal_port){.ctx = e,
                          .request = request,
                          .snapshot = snapshot,
                          .locked = locked,
                          .prepare_start = prepare,
                          .reset = reset,
                          .start_waiting = Zero_IsWaiting,
                          .start_failed = Zero_HasFailed,
                          .sensor_snapshot = Zero_Snapshot,
                          .begin_start = Zero_BeginStart};
}
