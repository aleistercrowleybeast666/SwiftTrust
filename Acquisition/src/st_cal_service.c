#include "st_cal_service.h"
#include <string.h>
void st_cal_service_init(st_cal_service *s, st_cal_profile *active, const st_cal_binding *binding,
                         st_cal_policy policy, st_cal_storage storage, void *ctx,
                         uint64_t (*now)(void *), bool (*idle)(void *))
{
    *s = (st_cal_service){.active = active,
                          .current = binding,
                          .policy = policy,
                          .storage = storage,
                          .ctx = ctx,
                          .now_us = now,
                          .idle = idle};
}
static bool configured(const st_cal_service *s)
{
    return s->active && s->current && s->now_us && s->idle && st_cal_binding_valid(*s->current);
}
static bool active_state(st_cal_state state)
{
    return state >= ST_CAL_ZERO_READY && state <= ST_CAL_REVIEW;
}
static bool request(void *ctx, st_cal_action action, const st_cal_reference *ref)
{
    st_cal_service *s = ctx;
    s->last_error = ST_CAL_OK;
    if (action == ST_CAL_ACTION_CANCEL)
    {
        st_cal_cancel(&s->workflow);
        return true;
    }
    if (!configured(s))
    {
        s->last_error = ST_CAL_CONFIG;
        return false;
    }
    bool idle = s->idle(s->ctx);
    uint64_t now = s->now_us(s->ctx);
    if (action == ST_CAL_ACTION_BEGIN)
        return st_cal_begin(&s->workflow, *s->current, s->policy, s->active, idle, now);
    if (!idle)
    {
        s->workflow.state = ST_CAL_FAILED;
        s->workflow.error = ST_CAL_BUSY;
        s->workflow.candidate.valid = false;
        return false;
    }
    if (!st_cal_binding_equal(*s->current, s->workflow.candidate.binding))
    {
        s->last_error = ST_CAL_CONFIG;
        return false;
    }
    switch (action)
    {
    case ST_CAL_ACTION_ZERO:
        return st_cal_start_zero(&s->workflow, idle, now);
    case ST_CAL_ACTION_REFERENCE_CLEAR:
        return st_cal_clear_reference(&s->workflow);
    case ST_CAL_ACTION_REFERENCE:
        return ref && st_cal_set_reference(&s->workflow, *ref);
    case ST_CAL_ACTION_LOAD:
        return st_cal_start_load(&s->workflow, idle, now);
    case ST_CAL_ACTION_SAVE:
        return st_cal_commit(&s->workflow, s->active, *s->current, idle, s->storage);
    default:
        return false;
    }
}
static bool snapshot(void *ctx, st_cal_view *out)
{
    st_cal_service *s = ctx;
    if (!out)
        return false;
    bool bound = configured(s), idle = bound && s->idle(s->ctx);
    if (bound)
    {
        if (!idle && active_state(s->workflow.state))
        {
            s->workflow.state = ST_CAL_FAILED;
            s->workflow.error = ST_CAL_BUSY;
            s->workflow.candidate.valid = false;
        }
        st_cal_tick(&s->workflow, s->now_us(s->ctx));
    }
    st_cal_profile *p = &s->workflow.candidate;
    *out = (st_cal_view){.state = s->workflow.state,
                         .error = s->workflow.error ? s->workflow.error : s->last_error,
                         .samples = s->workflow.window.count,
                         .required = s->policy.samples,
                         .revision = p->revision,
                         .active_revision = s->active ? s->active->revision : 0,
                         .zero_raw = p->zero_raw,
                         .loaded_raw = p->loaded_raw,
                         .slope_nn = p->nano_newtons_per_count,
                         .reference_force_nn = p->reference_force_nn,
                         .reference = p->reference,
                         .configured = bound,
                         .storage_bound = s->storage.save != NULL,
                         .idle = idle,
                         .simulated = s->current && s->current->simulated,
                         .have_reference = s->workflow.have_reference};
    return true;
}
st_cal_control_port st_cal_service_port(st_cal_service *s)
{
    return (st_cal_control_port){s, request, snapshot};
}
bool st_cal_service_feed(st_cal_service *s, const st_capture *capture)
{
    if (!configured(s) || !s->idle(s->ctx))
        return false;
    return st_cal_feed(&s->workflow, *s->current, capture);
}
