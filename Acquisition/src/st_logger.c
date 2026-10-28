#include "st_core.h"
#include <limits.h>
#include <string.h>
static bool valid_gain(uint8_t g)
{
    return g && g <= 128 && (g & (g - 1)) == 0;
}
void Log_ApplyAds1220Metadata(st_log_config *c, bool bypass, uint32_t measured)
{
    if (!c)
        return;
    c->adc_model = 1220;
    c->channel_count = 1;
    c->input_mux = 3; /* AIN1 positive, AIN2 negative */
    c->operating_mode = 2; /* Turbo */
    c->pga_bypass = bypass;
    c->reference_uv = ST_ADC_REFERENCE_UV;
    c->excitation_uv = ST_BRIDGE_EXCITATION_UV;
    c->nominal_rate_millihz = ST_ADC_NOMINAL_RATE_MILLIHZ;
    c->conversion_cycles = ST_ADC_CONVERSION_CYCLES;
    c->measured_rate_millihz = measured;
}
static void header_build(st_logger *l)
{
    uint8_t *h = l->header;
    const st_log_config *c = &l->config;
    memset(h, 0, 512);
    memcpy(h, "STLOG001", 8);
    st_put16(h + 8, 512);
    st_put16(h + 10, 32);
    st_put32(h + 12, c->adc_clock_hz);
    st_put32(h + 16, c->osr);
    st_put32(h + 20, c->rate_millihz);
    st_put32(h + 24, c->boot_epoch);
    st_put32(h + 28, c->profile_id);
    st_put32(h + 32, c->profile_version);
    st_put32(h + 36, (uint32_t)c->zero_raw);
    st_put64(h + 40, (uint64_t)c->nano_newtons_per_count);
    st_put64(h + 48, (uint64_t)c->nano_volts_per_count_ch1);
    h[56] = c->gain_ch0;
    h[57] = c->gain_ch1;
    h[58] = c->wire_count;
    h[59] = (uint8_t)((c->calibration_valid ? 1 : 0) | (c->simulated ? 2 : 0));
    if (c->session_zero_valid)
    {
        memcpy(h + 60, "SZR1", 4);
        st_put32(h + 64, (uint32_t)c->calibration_zero_raw);
        st_put32(h + 68, (uint32_t)c->session_zero_raw);
        st_put32(h + 72, c->zero_samples);
        st_put64(h + 76, c->zero_time_us);
    }
    /* ADC1 extends only previously reserved header bytes; fixed record offsets
     * and SZR1 are retained. INT32_MIN in the old ch1 slot means absent. */
    memcpy(h + 84, "ADC1", 4);
    st_put16(h + 88, 1);
    st_put16(h + 90, 44);
    st_put16(h + 92, c->adc_model);
    h[94] = c->channel_count;
    h[95] = 24;
    st_put32(h + 96, c->reference_uv);
    st_put32(h + 100, c->nominal_rate_millihz);
    st_put32(h + 104, c->conversion_cycles);
    st_put32(h + 108, c->measured_rate_millihz);
    h[112] = c->input_mux;
    h[113] = c->operating_mode;
    h[114] = c->pga_bypass ? 1 : 0;
    h[115] = 1; /* positive AIN1 */
    h[116] = 2; /* negative AIN2 */
    h[117] = 1; /* signed two's complement */
    st_put32(h + 120, c->excitation_uv);
    st_put32(h + 124, (c->clock_verified ? 1u : 0u) |
                         (c->measured_rate_millihz ? 2u : 0u));
    st_put32(h + 508, st_crc32(h, 508));
}
void st_logger_init(st_logger *l, st_log_config cfg, st_file_port port)
{
    memset(l, 0, sizeof(*l));
    st_ring_init(&l->ring);
    atomic_init(&l->capture_enabled, false);
    atomic_init(&l->source_errors, 0);
    l->config = cfg;
    l->port = port;
    l->state = ST_IDLE;
    l->active_stage = -1;
}
bool st_logger_start(st_logger *l, uint64_t now)
{
    const st_log_config *c = &l->config;
    if (l->state != ST_IDLE || l->generation == UINT32_MAX || !l->port.submit || !l->port.poll ||
        !c->boot_epoch || !c->clock_verified || c->adc_clock_hz != ST_ADC_CLOCK_HZ || c->osr ||
        c->rate_millihz != ST_ADC_RATE_MILLIHZ || !valid_gain(c->gain_ch0) || c->gain_ch1 ||
        c->adc_model != 1220 || c->channel_count != 1 || c->input_mux != 3 ||
        c->operating_mode != 2 || c->reference_uv != ST_ADC_REFERENCE_UV ||
        c->conversion_cycles != ST_ADC_CONVERSION_CYCLES ||
        c->nominal_rate_millihz != ST_ADC_NOMINAL_RATE_MILLIHZ ||
        c->excitation_uv != ST_BRIDGE_EXCITATION_UV || (c->pga_bypass && c->gain_ch0 > 4))
        return false;
    if (c->calibration_valid &&
        (!c->profile_id || !c->profile_version || !c->nano_newtons_per_count ||
         (c->wire_count != 4 && c->wire_count != 6)))
        return false;
    /* Both foreground and DMA producer must be quiescent before IDLE->STARTING. */
    uint32_t generation = l->generation + 1;
    st_log_config cfg = l->config;
    st_file_port port = l->port;
    st_logger_init(l, cfg, port);
    l->generation = generation;
    header_build(l);
    l->state = ST_STARTING;
    l->last_flush_us = now;
    l->last_sync_us = now;
    return true;
}
bool st_logger_stop(st_logger *l)
{
    if (l->state != ST_STARTING && l->state != ST_CAPTURING && l->state != ST_STOPPING)
        return l->state == ST_IDLE;
    atomic_store_explicit(&l->capture_enabled, false, memory_order_release);
    l->state = ST_STOPPING;
    return true;
}
void st_logger_source_error_isr(st_logger *l)
{
    uint32_t n = atomic_load_explicit(&l->source_errors, memory_order_relaxed);
    if (n != UINT32_MAX)
        atomic_store_explicit(&l->source_errors, n + 1, memory_order_relaxed);
}
uint32_t st_logger_error_count(const st_logger *l)
{
    uint64_t n =
        (uint64_t)l->adc_errors + atomic_load_explicit(&l->source_errors, memory_order_relaxed);
    return n > UINT32_MAX ? UINT32_MAX : (uint32_t)n;
}
bool st_logger_offer_isr(st_logger *l, const st_capture *c)
{
    if (!atomic_load_explicit(&l->capture_enabled, memory_order_acquire))
        return false;
    return st_ring_push(&l->ring, c);
}
static void fault(st_logger *l)
{
    atomic_store_explicit(&l->capture_enabled, false, memory_order_release);
    l->state = ST_FAULT;
}
static bool submit(st_logger *l, st_file_op op, const uint8_t *p, size_t n, int stage)
{
    st_async_result r = l->port.submit(l->port.ctx, op, p, n);
    if (r == ST_IO_ERROR)
    {
        fault(l);
        return false;
    }
    if (r == ST_IO_WAIT)
        return false;
    l->inflight = true;
    l->operation = op;
    l->active_stage = stage;
    l->submitted_bytes = n;
    if (op == ST_FILE_SYNC)
        l->sync_sequence = l->written_sequence;
    return true;
}
static bool stage_available(const st_logger *l, unsigned i)
{
    return !(l->inflight && l->operation == ST_FILE_WRITE && l->active_stage == (int)i) &&
           l->used[i] < ST_STAGE_BYTES;
}
static void footer(st_logger *l, unsigned i)
{
    if (!l->used[i])
        l->stage_order[i] = l->next_order++;
    uint8_t *p = l->stage[i] + l->used[i];
    memset(p, 0, 32);
    st_put32(p, l->last_sequence);
    st_put64(p + 4, l->last_time_us);
    st_put32(p + 12, atomic_load_explicit(&l->ring.dropped, memory_order_relaxed));
    st_put32(p + 16, st_logger_error_count(l));
    st_put16(p + 22, ST_REC_END);
    st_put32(p + 28, st_crc32(p, 28));
    l->used[i] += 32;
    l->footer_queued = true;
}
void st_logger_service(st_logger *l, uint64_t now)
{
    if (l->state == ST_IDLE || l->state == ST_FAULT)
        return;
    if (l->inflight)
    {
        size_t done = 0;
        st_async_result r = l->port.poll(l->port.ctx, &done);
        if (r == ST_IO_ERROR ||
            (r == ST_IO_DONE && l->operation == ST_FILE_WRITE && done != l->submitted_bytes))
        {
            fault(l);
            return;
        }
        if (r == ST_IO_DONE)
        {
            l->inflight = false;
            if (l->operation == ST_FILE_WRITE && l->active_stage >= 0)
            {
                unsigned i = (unsigned)l->active_stage;
                if (l->stage_has_sample[i])
                {
                    l->written_sequence = l->stage_sequence[i];
                    l->have_written = true;
                }
                l->used[i] = 0;
                l->stage_has_sample[i] = false;
                l->last_flush_us = now;
            }
            else if (l->operation == ST_FILE_SYNC)
            {
                if (l->have_written)
                {
                    l->durable_sequence = l->sync_sequence;
                    l->have_durable = true;
                }
                l->last_sync_us = now;
                if (l->phase == 2)
                {
                    l->phase = 3;
                    if (l->state != ST_STOPPING)
                    {
                        l->state = ST_CAPTURING;
                        atomic_store_explicit(&l->capture_enabled, true, memory_order_release);
                    }
                }
                else if (l->phase == 4)
                    l->phase = 5;
            }
            else if (l->operation == ST_FILE_CLOSE)
            {
                l->state = ST_IDLE;
                return;
            }
            else
                l->phase++;
        }
    }
    if (l->phase != 3)
    {
        if (l->inflight)
            return;
        if (l->phase == 0)
            (void)submit(l, ST_FILE_OPEN_NEW, NULL, 0, -1);
        else if (l->phase == 1)
            (void)submit(l, ST_FILE_WRITE, l->header, 512, -1);
        else if (l->phase == 2 || l->phase == 4)
            (void)submit(l, ST_FILE_SYNC, NULL, 0, -1);
        else if (l->phase == 5)
            (void)submit(l, ST_FILE_CLOSE, NULL, 0, -1);
        return;
    }
    for (unsigned batch = 0; batch < 64; batch++)
    {
        if (!stage_available(l, l->fill))
        {
            unsigned other = l->fill ^ 1u;
            if (!stage_available(l, other))
                break;
            l->fill = other;
        }
        st_capture c;
        if (!st_ring_pop(&l->ring, &c))
            break;
        uint16_t flags = 0;
        uint32_t missing = 0;
        if (l->have_sample)
        {
            uint32_t delta = c.sequence - l->last_sequence;
            if (delta != 1)
            {
                flags |= ST_REC_GAP;
                missing = delta > 1 && delta < 0x80000000u ? delta - 1 : UINT32_MAX;
            }
            if (c.time_us <= l->last_time_us)
                flags |= ST_REC_TIME;
        }
        unsigned i = l->fill;
        if (!l->used[i])
            l->stage_order[i] = l->next_order++;
        uint8_t *p = l->stage[i] + l->used[i];
        st_record(p, &c, flags, missing);
        if (st_get16(p + 22) & (ST_REC_CRC | ST_REC_FORMAT | ST_REC_ADC_FAULT))
        {
            if (l->adc_errors != UINT32_MAX)
                l->adc_errors++;
        }
        l->used[i] += 32;
        l->stage_sequence[i] = c.sequence;
        l->stage_has_sample[i] = true;
        l->last_sequence = c.sequence;
        l->last_time_us = c.time_us;
        l->have_sample = true;
    }
    if (l->state == ST_STOPPING && !st_ring_count(&l->ring) && !l->footer_queued)
    {
        if (stage_available(l, l->fill))
            footer(l, l->fill);
        else if (stage_available(l, l->fill ^ 1u))
            footer(l, l->fill ^ 1u);
    }
    if (l->inflight)
        return;
    /* Submit in sample order, including when partial buffers were flushed. */
    int selected = -1;
    for (unsigned i = 0; i < 2; i++)
        if (l->used[i] && (l->used[i] == ST_STAGE_BYTES || l->state == ST_STOPPING ||
                           now - l->last_flush_us >= 500000))
        {
            if (selected < 0 || l->stage_order[i] < l->stage_order[(unsigned)selected])
                selected = (int)i;
        }
    if (selected >= 0)
    {
        unsigned i = (unsigned)selected;
        (void)submit(l, ST_FILE_WRITE, l->stage[i], l->used[i], selected);
        return;
    }
    if (l->state == ST_STOPPING && l->footer_queued && !l->used[0] && !l->used[1])
    {
        l->phase = 4;
        (void)submit(l, ST_FILE_SYNC, NULL, 0, -1);
        return;
    }
    if (l->have_written && now - l->last_sync_us >= 1000000)
        (void)submit(l, ST_FILE_SYNC, NULL, 0, -1);
}
