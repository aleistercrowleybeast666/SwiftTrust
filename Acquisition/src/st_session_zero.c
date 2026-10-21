#include "st_session_zero.h"
SessionZeroResult SessionZero_Begin(StSessionZero *zero, bool idle, uint64_t now, uint32_t span)
{
    if (!zero || !span)
        return ST_ZERO_INVALID;
    if (!idle || zero->state == ST_ZERO_COLLECTING)
        return ST_ZERO_BUSY;
    *zero = (StSessionZero){.state = ST_ZERO_COLLECTING, .began_us = now, .max_span = span};
    return ST_ZERO_OK;
}
void SessionZero_Tick(StSessionZero *zero, uint64_t now)
{
    if (zero && zero->state == ST_ZERO_COLLECTING &&
        now - zero->began_us > ST_ZERO_TIMEOUT_MS * UINT64_C(1000))
        zero->state = ST_ZERO_FAILED;
}
void SessionZero_Feed(StSessionZero *zero, const st_capture *capture)
{
    if (!zero || !capture || zero->state != ST_ZERO_COLLECTING || capture->time_us < zero->began_us)
        return;
    SessionZero_Tick(zero, capture->time_us);
    if (zero->state != ST_ZERO_COLLECTING)
        return;
    st_adc_sample sample = st_adc_decode(capture->bytes);
    if (sample.flags || (zero->count && (capture->sequence != zero->sequence + 1u ||
                                         capture->time_us <= zero->last_us)))
    {
        zero->state = ST_ZERO_FAILED;
        return;
    }
    if (!zero->count)
        zero->minimum = zero->maximum = sample.ch0;
    if (sample.ch0 < zero->minimum)
        zero->minimum = sample.ch0;
    if (sample.ch0 > zero->maximum)
        zero->maximum = sample.ch0;
    if ((int64_t)zero->maximum - zero->minimum > zero->max_span)
    {
        zero->state = ST_ZERO_FAILED;
        return;
    }
    zero->sum += sample.ch0;
    zero->count++;
    zero->sequence = capture->sequence;
    zero->last_us = capture->time_us;
    if (zero->count == ST_ZERO_WINDOW_SAMPLES)
    {
        zero->mean = (int32_t)(zero->sum / (int64_t)zero->count);
        zero->state = ST_ZERO_READY;
    }
}
SessionZeroResult SessionZero_Apply(const StSessionZero *zero, st_logger *logger)
{
    if (!zero || !logger || zero->state != ST_ZERO_READY)
        return ST_ZERO_INVALID;
    if (logger->state != ST_IDLE || logger->inflight || atomic_load(&logger->capture_enabled))
        return ST_ZERO_BUSY;
    logger->config.calibration_zero_raw = logger->config.zero_raw;
    logger->config.zero_raw = zero->mean;
    logger->config.session_zero_raw = zero->mean;
    logger->config.session_zero_valid = true;
    logger->config.zero_samples = zero->count;
    logger->config.zero_time_us = zero->last_us;
    return ST_ZERO_OK;
}
