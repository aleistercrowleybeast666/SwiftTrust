#include "st_cal_endpoint.h"
#include <string.h>
void st_cal_view_encode(const st_cal_view *v, uint8_t p[64])
{
    memset(p, 0, 64);
    p[0] = (uint8_t)v->state;
    p[1] = (uint8_t)v->error;
    p[2] =
        (uint8_t)((v->configured ? 1u : 0u) | (v->storage_bound ? 2u : 0u) | (v->idle ? 4u : 0u) |
                  (v->simulated ? 8u : 0u) | (v->have_reference ? 16u : 0u));
    p[3] = v->request_failed ? 1 : 0;
    st_put32(p + 4, v->samples);
    st_put32(p + 8, v->required);
    st_put32(p + 12, v->revision);
    st_put32(p + 16, v->active_revision);
    st_put32(p + 20, (uint32_t)v->zero_raw);
    st_put32(p + 24, (uint32_t)v->loaded_raw);
    st_put64(p + 28, (uint64_t)v->slope_nn);
    st_put64(p + 36, (uint64_t)v->reference_force_nn);
    p[44] = (uint8_t)v->reference.unit;
    st_put64(p + 45, v->reference.milli_units);
    st_put32(p + 53, v->reference.gravity_um_s2);
}
bool st_cal_view_decode(st_cal_view *v, const uint8_t p[64])
{
    if (p[0] > ST_CAL_FAILED || p[1] > ST_CAL_LINK || (p[2] & ~31u) || p[3] > 1 || p[44] > 2)
        return false;
    for (unsigned i = 57; i < 64; i++)
        if (p[i])
            return false;
    *v = (st_cal_view){.state = (st_cal_state)p[0],
                       .error = (st_cal_error)p[1],
                       .configured = (p[2] & 1u) != 0,
                       .storage_bound = (p[2] & 2u) != 0,
                       .idle = (p[2] & 4u) != 0,
                       .simulated = (p[2] & 8u) != 0,
                       .have_reference = (p[2] & 16u) != 0,
                       .request_failed = p[3] != 0,
                       .samples = st_get32(p + 4),
                       .required = st_get32(p + 8),
                       .revision = st_get32(p + 12),
                       .active_revision = st_get32(p + 16),
                       .zero_raw = (int32_t)st_get32(p + 20),
                       .loaded_raw = (int32_t)st_get32(p + 24),
                       .slope_nn = (int64_t)st_get64(p + 28),
                       .reference_force_nn = (int64_t)st_get64(p + 36),
                       .reference = {(st_cal_unit)p[44], st_get64(p + 45), st_get32(p + 53)}};
    return v->samples <= ST_CAL_MAX_SAMPLES && v->required <= ST_CAL_MAX_SAMPLES;
}
