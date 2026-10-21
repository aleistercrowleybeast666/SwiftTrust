#include "st_cal_remote.h"
#include <string.h>
void st_cal_remote_init(st_cal_remote *r, st2_client *c, void *ctx, uint32_t (*now)(void *))
{
    *r = (st_cal_remote){.link = c, .ctx = ctx, .now_ms = now};
}
void st_cal_remote_step(st_cal_remote *r, bool visible)
{
    if (!r || !r->link || !r->now_ms || !visible)
        return;
    uint32_t now = r->now_ms(r->ctx);
    if (!r->link->pending && (!r->link->cal_have_view ||
                              (uint32_t)(now - r->last_poll_ms) >= r->link->timing.heartbeat_ms))
    {
        if (st2_client_cal_command(r->link, NULL, now))
            r->last_poll_ms = now;
    }
}
static bool request(void *ctx, st_cal_action action, const st_cal_reference *ref)
{
    st_cal_remote *r = ctx;
    if (!r->now_ms || !r->link || (unsigned)action > ST_CAL_ACTION_REFERENCE_CLEAR)
        return false;
    uint8_t p[14] = {0};
    p[0] = (uint8_t)action;
    if (action == ST_CAL_ACTION_REFERENCE)
    {
        int64_t force;
        if (!ref || !st_cal_reference_force(*ref, &force))
            return false;
        p[1] = (uint8_t)ref->unit;
        st_put64(p + 2, ref->milli_units);
        st_put32(p + 10, ref->gravity_um_s2);
    }
    bool ok = st2_client_cal_command(r->link, p, r->now_ms(r->ctx));
    if (ok)
        r->awaiting_result = true;
    return ok;
}
static bool snapshot(void *ctx, st_cal_view *v)
{
    st_cal_remote *r = ctx;
    if (!v || !r->link || !r->now_ms)
        return false;
    st2_client *c = r->link;
    uint32_t now = r->now_ms(r->ctx);

    if (r->cache_epoch != c->epoch)
    {
        r->have_cache = false;
        r->cache_epoch = c->epoch;
    }
    bool pending =
        c->pending && (c->request.type == ST2_CAL_COMMAND || c->request.type == ST2_CAL_QUERY);
    st_cal_view decoded;
    bool valid = c->cal_have_view && st_cal_view_decode(&decoded, c->cal_view);
    if (valid)
    {
        r->cached = decoded;
        r->have_cache = true;
        r->awaiting_result = false;
    }
    bool background =
        pending && c->request.type == ST2_CAL_QUERY && r->have_cache && !r->awaiting_result;
    *v = r->have_cache ? r->cached : (st_cal_view){.error = ST_CAL_LINK};
    /* A routine read-only refresh does not freeze digit editing. A command's
     * uncertain outcome can never be hidden by an older cached SAVED screen. */
    v->pending = pending && !background;
    bool fresh =
        st2_client_fresh(c, now) && (uint32_t)(now - c->cal_received_ms) <= ST_LINK_LOST_MS;
    if (!v->pending && (!fresh || (!valid && !background)))
    {
        v->state = ST_CAL_FAILED;
        v->error = ST_CAL_LINK;
        v->uncertain = true;
        v->configured = false;
    }
    return true;
}
st_cal_control_port st_cal_remote_port(st_cal_remote *r)
{
    return (st_cal_control_port){r, request, snapshot};
}
