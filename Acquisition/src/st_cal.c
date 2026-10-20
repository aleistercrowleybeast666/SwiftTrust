#include "st_cal.h"
#include <limits.h>
#include <string.h>
enum
{
    CAL_CRC = 252,
    CAL_RESERVED = 176
};
static bool gain_valid(uint8_t gain)
{
    return gain && gain <= 128 && !(gain & (gain - 1u));
}
bool st_cal_binding_valid(st_cal_binding b)
{
    return b.verified && b.profile_id && b.sensor_id && b.config_id && b.filter_id &&
           b.excitation_nominal_uv && b.adc_clock_hz && b.adc_clock_hz <= 8400000 &&
           b.rate_millihz && b.rated_force_nn && b.rated_force_nn <= (uint64_t)INT64_MAX / 2 &&
           (b.osr == 64 || (b.osr >= 128 && b.osr <= 8192 && !(b.osr & (b.osr - 1u)))) &&
           gain_valid(b.gain0) && gain_valid(b.gain1) && (b.wire_count == 4 || b.wire_count == 6) &&
           b.power_mode <= 2;
}
bool st_cal_binding_equal(st_cal_binding a, st_cal_binding b)
{
    return a.profile_id == b.profile_id && a.sensor_id == b.sensor_id &&
           a.config_id == b.config_id && a.filter_id == b.filter_id &&
           a.excitation_nominal_uv == b.excitation_nominal_uv &&
           a.rated_force_nn == b.rated_force_nn && a.adc_clock_hz == b.adc_clock_hz &&
           a.osr == b.osr && a.rate_millihz == b.rate_millihz && a.gain0 == b.gain0 &&
           a.gain1 == b.gain1 && a.wire_count == b.wire_count && a.power_mode == b.power_mode &&
           a.verified == b.verified && a.simulated == b.simulated;
}
bool st_cal_reference_force(st_cal_reference ref, int64_t *force)
{
    if (!force || !ref.milli_units || (unsigned)ref.unit > ST_CAL_NEWTONS)
        return false;
    uint64_t result;
    if (ref.unit == ST_CAL_NEWTONS)
    {
        if (ref.gravity_um_s2 || ref.milli_units > (uint64_t)INT64_MAX / 1000000)
            return false;
        result = ref.milli_units * 1000000;
    }
    else
    {
        if (ref.gravity_um_s2 < 9000000 || ref.gravity_um_s2 > 11000000)
            return false;
        uint64_t mass_mg = ref.milli_units;
        if (ref.unit == ST_CAL_KILOGRAMS)
        {
            if (mass_mg > UINT64_MAX / 1000)
                return false;
            mass_mg *= 1000;
        }
        if (mass_mg > (UINT64_MAX - 500) / ref.gravity_um_s2)
            return false;
        result = (mass_mg * ref.gravity_um_s2 + 500) / 1000; /* nearest integer nN */
    }
    if (!result || result > (uint64_t)INT64_MAX)
        return false;
    *force = (int64_t)result;
    return true;
}
static bool policy_valid(st_cal_policy p)
{
    return p.samples >= ST_CAL_MIN_SAMPLES && p.samples <= ST_CAL_MAX_SAMPLES &&
           p.max_span_counts && p.max_span_counts <= 16777215 &&
           p.min_delta_counts > p.max_span_counts && p.min_delta_counts <= 16777215 &&
           p.timeout_us > p.settle_us && p.timeout_us <= 60000000 &&
           p.zero_max_age_us >= p.timeout_us && p.zero_max_age_us <= 3600000000u;
}
static int32_t rounded_mean(int64_t sum, uint32_t count)
{
    int64_t n = (int64_t)count;
    return (int32_t)(sum >= 0 ? (sum + n / 2) / n : -((-sum + n / 2) / n));
}
static int64_t slope_for(int64_t force, int32_t zero, int32_t loaded)
{
    int64_t delta = (int64_t)loaded - zero, span = delta < 0 ? -delta : delta;
    int64_t slope = (force + span / 2) / span;
    return delta < 0 ? -slope : slope;
}
static bool profile_valid(const st_cal_profile *p)
{
    int64_t force;
    if (!p || !p->valid || !p->revision || !st_cal_binding_valid(p->binding) ||
        !policy_valid(p->policy) || !st_cal_reference_force(p->reference, &force) ||
        force != p->reference_force_nn || (uint64_t)force > p->binding.rated_force_nn ||
        p->zero.count != p->policy.samples || p->loaded.count != p->policy.samples ||
        p->completed_us < p->began_us || p->zero.last_us < p->began_us ||
        p->completed_us < p->zero.last_us ||
        p->completed_us - p->zero.last_us > p->policy.zero_max_age_us)
        return false;
    const st_cal_window *windows[] = {&p->zero, &p->loaded};
    for (unsigned i = 0; i < 2; i++)
    {
        const st_cal_window *w = windows[i];
        if (w->minimum < -8388608 || w->maximum > 8388607 || w->minimum > w->mean ||
            w->maximum < w->mean || (int64_t)w->maximum - w->minimum > p->policy.max_span_counts)
            return false;
    }
    int64_t delta = (int64_t)p->loaded_raw - p->zero_raw, span = delta < 0 ? -delta : delta;
    return p->zero_raw == p->zero.mean && p->loaded_raw == p->loaded.mean &&
           span >= p->policy.min_delta_counts && p->nano_newtons_per_count &&
           p->nano_newtons_per_count == slope_for(force, p->zero_raw, p->loaded_raw);
}
size_t st_cal_profile_encode(const st_cal_profile *p, uint8_t out[ST_CAL_PROFILE_BYTES])
{
    if (!profile_valid(p) || !out)
        return 0;
    memset(out, 0, ST_CAL_PROFILE_BYTES);
    memcpy(out, "STCAL001", 8);
    st_put16(out + 8, ST_CAL_PROFILE_BYTES);
    st_put16(out + 10, (uint16_t)(1u | (p->binding.simulated ? 2u : 0u) | 4u));
    const st_cal_binding *b = &p->binding;
    st_put32(out + 12, b->profile_id);
    st_put32(out + 16, p->revision);
    st_put32(out + 20, b->sensor_id);
    st_put32(out + 24, b->config_id);
    st_put32(out + 28, b->adc_clock_hz);
    st_put32(out + 32, b->osr);
    st_put32(out + 36, b->rate_millihz);
    st_put32(out + 40, b->excitation_nominal_uv);
    st_put32(out + 44, b->filter_id);
    out[48] = b->gain0;
    out[49] = b->gain1;
    out[50] = b->wire_count;
    out[51] = b->power_mode;
    st_put32(out + 52, (uint32_t)p->zero_raw);
    st_put32(out + 56, (uint32_t)p->loaded_raw);
    st_put64(out + 60, (uint64_t)p->nano_newtons_per_count);
    out[68] = (uint8_t)p->reference.unit;
    st_put64(out + 72, p->reference.milli_units);
    st_put32(out + 80, p->reference.gravity_um_s2);
    st_put64(out + 88, (uint64_t)p->reference_force_nn);
    st_put32(out + 96, p->zero.count);
    st_put32(out + 100, p->loaded.count);
    st_put32(out + 104, (uint32_t)p->zero.minimum);
    st_put32(out + 108, (uint32_t)p->zero.maximum);
    st_put32(out + 112, (uint32_t)p->loaded.minimum);
    st_put32(out + 116, (uint32_t)p->loaded.maximum);
    st_put64(out + 120, p->began_us);
    st_put64(out + 128, p->completed_us);
    st_put32(out + 136, p->policy.max_span_counts);
    st_put32(out + 140, p->policy.min_delta_counts);
    st_put32(out + 144, p->policy.settle_us);
    st_put32(out + 148, p->policy.timeout_us);
    st_put32(out + 152, p->policy.samples);
    st_put64(out + 156, b->rated_force_nn);
    st_put32(out + 164, p->policy.zero_max_age_us);
    st_put64(out + 168, p->zero.last_us);
    st_put32(out + CAL_CRC, st_crc32(out, CAL_CRC));
    return ST_CAL_PROFILE_BYTES;
}
bool st_cal_profile_decode(st_cal_profile *p, const uint8_t *in, size_t n)
{
    if (!p || !in || n != ST_CAL_PROFILE_BYTES || memcmp(in, "STCAL001", 8) ||
        st_get16(in + 8) != n || (st_get16(in + 10) & ~7u) || (st_get16(in + 10) & 5u) != 5u ||
        st_get32(in + CAL_CRC) != st_crc32(in, CAL_CRC))
        return false;
    if (in[69] || in[70] || in[71] || st_get32(in + 84))
        return false;
    for (unsigned i = CAL_RESERVED; i < CAL_CRC; i++)
        if (in[i])
            return false;
    st_cal_profile result = {0};
    st_cal_binding *b = &result.binding;
    b->profile_id = st_get32(in + 12);
    result.revision = st_get32(in + 16);
    b->sensor_id = st_get32(in + 20);
    b->config_id = st_get32(in + 24);
    b->adc_clock_hz = st_get32(in + 28);
    b->osr = st_get32(in + 32);
    b->rate_millihz = st_get32(in + 36);
    b->excitation_nominal_uv = st_get32(in + 40);
    b->filter_id = st_get32(in + 44);
    b->gain0 = in[48];
    b->gain1 = in[49];
    b->wire_count = in[50];
    b->power_mode = in[51];
    b->simulated = (st_get16(in + 10) & 2u) != 0;
    b->verified = true;
    b->rated_force_nn = st_get64(in + 156);
    result.zero_raw = (int32_t)st_get32(in + 52);
    result.loaded_raw = (int32_t)st_get32(in + 56);
    result.nano_newtons_per_count = (int64_t)st_get64(in + 60);
    result.reference =
        (st_cal_reference){(st_cal_unit)in[68], st_get64(in + 72), st_get32(in + 80)};
    result.reference_force_nn = (int64_t)st_get64(in + 88);
    result.zero.count = st_get32(in + 96);
    result.loaded.count = st_get32(in + 100);
    result.zero.minimum = (int32_t)st_get32(in + 104);
    result.zero.maximum = (int32_t)st_get32(in + 108);
    result.loaded.minimum = (int32_t)st_get32(in + 112);
    result.loaded.maximum = (int32_t)st_get32(in + 116);
    result.zero.mean = result.zero_raw;
    result.loaded.mean = result.loaded_raw;
    result.began_us = st_get64(in + 120);
    result.completed_us = st_get64(in + 128);
    result.policy = (st_cal_policy){st_get32(in + 152), st_get32(in + 136), st_get32(in + 140),
                                    st_get32(in + 144), st_get32(in + 148), st_get32(in + 164)};
    result.zero.last_us = st_get64(in + 168);
    result.loaded.last_us = result.completed_us;
    result.valid = true;
    if (!profile_valid(&result))
        return false;
    *p = result;
    return true;
}
static uint32_t fingerprint(const st_cal_profile *p)
{
    uint8_t bytes[ST_CAL_PROFILE_BYTES];
    return st_cal_profile_encode(p, bytes) ? st_get32(bytes + CAL_CRC) : 0;
}
static bool fail(st_cal_workflow *w, st_cal_error error)
{
    w->state = ST_CAL_FAILED;
    w->error = error;
    w->candidate.valid = false;
    return false;
}
bool st_cal_begin(st_cal_workflow *w, st_cal_binding b, st_cal_policy policy,
                  const st_cal_profile *active, bool idle, uint64_t now)
{
    if (!w || !active)
        return false;
    *w = (st_cal_workflow){.state = ST_CAL_INACTIVE};
    if (!idle)
        return fail(w, ST_CAL_BUSY);
    if (!st_cal_binding_valid(b) || !policy_valid(policy))
        return fail(w, ST_CAL_CONFIG);
    if (active->valid && !profile_valid(active))
        return fail(w, ST_CAL_CONFLICT);
    if (active->valid && active->binding.profile_id == b.profile_id &&
        active->revision == UINT32_MAX)
        return fail(w, ST_CAL_CONFLICT);
    w->candidate.binding = b;
    w->candidate.policy = policy;
    w->candidate.began_us = now;
    w->candidate.revision =
        active->valid && active->binding.profile_id == b.profile_id ? active->revision + 1 : 1;
    w->expected_active_crc = fingerprint(active);
    w->state = ST_CAL_ZERO_READY;
    return true;
}
static bool start_window(st_cal_workflow *w, bool idle, uint64_t now, st_cal_state required,
                         st_cal_state next)
{
    if (!w || w->state != required)
        return false;
    if (!idle)
        return fail(w, ST_CAL_BUSY);
    if (now < w->candidate.began_us)
        return fail(w, ST_CAL_CLOCK);
    w->window = (st_cal_window){0};
    w->collect_us = now;
    w->state = next;
    return true;
}
bool st_cal_start_zero(st_cal_workflow *w, bool idle, uint64_t now)
{
    return start_window(w, idle, now, ST_CAL_ZERO_READY, ST_CAL_ZERO_COLLECT);
}
bool st_cal_clear_reference(st_cal_workflow *w)
{
    if (!w || w->state != ST_CAL_LOAD_READY)
        return false;
    w->have_reference = false;
    w->candidate.reference = (st_cal_reference){0};
    w->candidate.reference_force_nn = 0;
    w->error = ST_CAL_OK;
    return true;
}
bool st_cal_set_reference(st_cal_workflow *w, st_cal_reference ref)
{
    if (!w || w->state != ST_CAL_LOAD_READY)
        return false;
    int64_t force;
    if (!st_cal_reference_force(ref, &force) ||
        (uint64_t)force > w->candidate.binding.rated_force_nn)
    {
        w->error = ST_CAL_REFERENCE;
        return false;
    }
    w->candidate.reference = ref;
    w->candidate.reference_force_nn = force;
    w->have_reference = true;
    w->error = ST_CAL_OK;
    return true;
}
bool st_cal_start_load(st_cal_workflow *w, bool idle, uint64_t now)
{
    if (!w || !w->have_reference)
        return false;
    if (now < w->candidate.zero.last_us ||
        now - w->candidate.zero.last_us > w->candidate.policy.zero_max_age_us)
        return fail(w, ST_CAL_ZERO_EXPIRED);
    return start_window(w, idle, now, ST_CAL_LOAD_READY, ST_CAL_LOAD_COLLECT);
}
void st_cal_tick(st_cal_workflow *w, uint64_t now)
{
    if (!w || (w->state != ST_CAL_ZERO_COLLECT && w->state != ST_CAL_LOAD_COLLECT))
        return;
    if (now < w->collect_us)
        (void)fail(w, ST_CAL_CLOCK);
    else if (now - w->collect_us > w->candidate.policy.timeout_us)
        (void)fail(w, ST_CAL_TIMEOUT);
}
bool st_cal_feed(st_cal_workflow *w, st_cal_binding b, const st_capture *capture)
{
    if (!w || !capture || (w->state != ST_CAL_ZERO_COLLECT && w->state != ST_CAL_LOAD_COLLECT))
        return false;
    if (!st_cal_binding_equal(b, w->candidate.binding))
        return fail(w, ST_CAL_CONFIG);
    st_cal_tick(w, capture->time_us);
    if (w->state == ST_CAL_FAILED)
        return false;
    st_adc_sample sample = st_adc_decode(capture->bytes);
    if (sample.flags)
        return fail(w, ST_CAL_ADC);
    if (capture->time_us - w->collect_us < w->candidate.policy.settle_us)
        return true;
    st_cal_window *window = &w->window;
    if (window->count &&
        (capture->sequence - window->last_sequence != 1 || capture->time_us <= window->last_us))
        return fail(w, ST_CAL_SEQUENCE);
    if (!window->count)
        window->minimum = window->maximum = sample.ch0;
    if (sample.ch0 < window->minimum)
        window->minimum = sample.ch0;
    if (sample.ch0 > window->maximum)
        window->maximum = sample.ch0;
    if ((int64_t)window->maximum - window->minimum > w->candidate.policy.max_span_counts)
        return fail(w, ST_CAL_UNSTABLE);
    window->sum += sample.ch0;
    window->count++;
    window->last_us = capture->time_us;
    window->last_sequence = capture->sequence;
    if (window->count == w->candidate.policy.samples)
    {
        window->mean = rounded_mean(window->sum, window->count);
        if (w->state == ST_CAL_ZERO_COLLECT)
        {
            w->candidate.zero = *window;
            w->candidate.zero_raw = window->mean;
            w->state = ST_CAL_LOAD_READY;
        }
        else
        {
            w->candidate.loaded = *window;
            w->candidate.loaded_raw = window->mean;
            int64_t delta = (int64_t)window->mean - w->candidate.zero_raw,
                    span = delta < 0 ? -delta : delta;
            if (span < w->candidate.policy.min_delta_counts)
                return fail(w, ST_CAL_SPAN);
            w->candidate.nano_newtons_per_count =
                slope_for(w->candidate.reference_force_nn, w->candidate.zero_raw, window->mean);
            if (!w->candidate.nano_newtons_per_count)
                return fail(w, ST_CAL_COEFFICIENT);
            w->candidate.completed_us = capture->time_us;
            w->candidate.valid = true;
            if (!profile_valid(&w->candidate))
                return fail(w, ST_CAL_SPAN);
            w->state = ST_CAL_REVIEW;
        }
    }
    return true;
}
void st_cal_cancel(st_cal_workflow *w)
{
    if (w)
    {
        w->candidate.valid = false;
        w->state = ST_CAL_CANCELLED;
        w->error = ST_CAL_OK;
    }
}
bool st_cal_commit(st_cal_workflow *w, st_cal_profile *active, st_cal_binding current, bool idle,
                   st_cal_storage storage)
{
    if (!w || !active || w->state != ST_CAL_REVIEW)
        return false;
    if (!idle)
        return fail(w, ST_CAL_BUSY);
    if (!st_cal_binding_equal(current, w->candidate.binding))
        return fail(w, ST_CAL_CONFIG);
    if ((active->valid && !profile_valid(active)) || fingerprint(active) != w->expected_active_crc)
        return fail(w, ST_CAL_CONFLICT);
    uint8_t bytes[ST_CAL_PROFILE_BYTES];
    if (!st_cal_profile_encode(&w->candidate, bytes))
        return fail(w, ST_CAL_CONFIG);
    if (!storage.save)
        return fail(w, ST_CAL_STORAGE_UNBOUND);
    if (!storage.save(storage.ctx, bytes))
        return fail(w, ST_CAL_STORAGE_ERROR);
    *active = w->candidate;
    w->state = ST_CAL_COMMITTED;
    w->error = ST_CAL_OK;
    return true;
}
bool st_cal_apply_logger(const st_cal_profile *p, st_cal_binding b, st_logger *logger)
{
    if (!logger || logger->state != ST_IDLE || logger->inflight ||
        atomic_load(&logger->capture_enabled))
        return false;
    st_log_config *cfg = &logger->config;
    if (!profile_valid(p) || !st_cal_binding_equal(p->binding, b) || !st_cal_binding_valid(b) ||
        !cfg->clock_verified || cfg->adc_clock_hz != b.adc_clock_hz || cfg->osr != b.osr ||
        cfg->rate_millihz != b.rate_millihz || cfg->gain_ch0 != b.gain0 ||
        cfg->gain_ch1 != b.gain1 || cfg->simulated != b.simulated)
    {
        cfg->calibration_valid = false;
        return false;
    }
    cfg->profile_id = b.profile_id;
    cfg->profile_version = p->revision;
    cfg->zero_raw = p->zero_raw;
    cfg->nano_newtons_per_count = p->nano_newtons_per_count;
    cfg->wire_count = b.wire_count;
    cfg->calibration_valid = true;
    return true;
}
