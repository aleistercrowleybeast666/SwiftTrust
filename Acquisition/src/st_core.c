#include "st_core.h"
#include <limits.h>
#include <string.h>
uint16_t st_get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
uint32_t st_get32(const uint8_t *p)
{
    return (uint32_t)st_get16(p) | ((uint32_t)st_get16(p + 2) << 16);
}
uint64_t st_get64(const uint8_t *p)
{
    return (uint64_t)st_get32(p) | ((uint64_t)st_get32(p + 4) << 32);
}
void st_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
void st_put32(uint8_t *p, uint32_t v)
{
    st_put16(p, (uint16_t)v);
    st_put16(p + 2, (uint16_t)(v >> 16));
}
void st_put64(uint8_t *p, uint64_t v)
{
    st_put32(p, (uint32_t)v);
    st_put32(p + 4, (uint32_t)(v >> 32));
}
uint16_t st_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xffff;
    while (n--)
    {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (unsigned i = 0; i < 8; i++)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
uint32_t st_crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = UINT32_MAX;
    while (n--)
    {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
bool st_seq_newer(uint32_t a, uint32_t b)
{
    uint32_t d = a - b;
    return d != 0 && d < 0x80000000u;
}
static int32_t signed24(const uint8_t *p)
{
    uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return (v & 0x800000u) ? (int32_t)(v & 0x7fffffu) - 8388608 : (int32_t)v;
}
st_adc_sample st_adc_decode(const uint8_t f[12])
{
    /* ADS1220 supplies neither status word nor CRC. Transport errors are
     * reported by the DMA owner; a successful read cannot prove analog validity. */
    st_adc_sample s = {signed24(f), INT32_MIN, 0, 0};
    for (unsigned i = 3; i < 12; i++)
        if (f[i])
            s.flags |= ST_REC_FORMAT;
    if (s.ch0 == 8388607 || s.ch0 == -8388608)
        s.flags |= ST_REC_SATURATED;
    return s;
}
void st_record(uint8_t out[32], const st_capture *c, uint16_t flags, uint32_t missing)
{
    st_adc_sample s = st_adc_decode(c->bytes);
    st_put32(out, c->sequence);
    st_put64(out + 4, c->time_us);
    st_put32(out + 12, (uint32_t)s.ch0);
    st_put32(out + 16, (uint32_t)s.ch1);
    st_put16(out + 20, s.status);
    st_put16(out + 22, (uint16_t)(flags | s.flags));
    st_put32(out + 24, missing);
    st_put32(out + 28, st_crc32(out, 28));
}
void st_ring_init(st_ring *q)
{
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    atomic_init(&q->dropped, 0);
}
uint32_t st_ring_count(const st_ring *q)
{
    return atomic_load_explicit(&q->head, memory_order_acquire) -
           atomic_load_explicit(&q->tail, memory_order_acquire);
}
bool st_ring_push(st_ring *q, const st_capture *v)
{
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t == ST_RING_CAPACITY)
    {
        uint32_t d = atomic_load_explicit(&q->dropped, memory_order_relaxed);
        if (d != UINT32_MAX)
            atomic_store_explicit(&q->dropped, d + 1, memory_order_relaxed);
        return false;
    }
    q->entries[h & (ST_RING_CAPACITY - 1)] = *v;
    atomic_store_explicit(&q->head, h + 1, memory_order_release);
    return true;
}
bool st_ring_pop(st_ring *q, st_capture *v)
{
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&q->head, memory_order_acquire))
        return false;
    *v = q->entries[t & (ST_RING_CAPACITY - 1)];
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);
    return true;
}
st_voltage st_voltage_from_adc(uint16_t code, uint32_t reference_mv, uint32_t numerator,
                               uint32_t denominator, uint64_t time_us, bool present, bool verified)
{
    st_voltage v = {.time_us = time_us, .present = present};
    if (!present || !verified || code > 4095 || !reference_mv || reference_mv > 5500 ||
        !numerator || numerator > 1000000 || !denominator || denominator > 1000000)
        return v;
    uint64_t divisor = 4095u * (uint64_t)denominator;
    uint64_t mv = ((uint64_t)code * reference_mv * numerator + divisor / 2) / divisor;
    if (mv > UINT32_MAX)
        return v;
    v.mv = (uint32_t)mv;
    v.valid = true;
    return v;
}
bool st_voltage_policy_valid(const st_voltage_policy *p)
{
    if (!p->bound || !p->freshness_ms)
        return false;
    for (unsigned i = 0; i < 4; i++)
    {
        if (!p->threshold_mv[i] || (i && p->threshold_mv[i] <= p->threshold_mv[i - 1]))
            return false;
        if (i && p->hysteresis_mv >= (p->threshold_mv[i] - p->threshold_mv[i - 1]) / 2)
            return false;
    }
    return true;
}
st_voltage_view st_voltage_evaluate(const st_voltage_policy *p, const st_voltage *v, uint64_t now,
                                    st_voltage_view old)
{
    st_voltage_view r = {ST_V_UNKNOWN, 0, 0, v->mv, UINT32_MAX};
    if (!v->present || !v->valid || now < v->time_us)
        return r;
    uint64_t age = (now - v->time_us) / 1000;
    r.age_ms = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
    if (!p->freshness_ms || age > p->freshness_ms)
    {
        r.kind = ST_V_STALE;
        return r;
    }
    r.kind = ST_V_MEASURED;
    if (!st_voltage_policy_valid(p))
        return r; /* measured mV, unknown tier */
    unsigned tier = 0;
    if (old.kind == ST_V_MEASURED || old.kind == ST_V_LOW)
    {
        tier = old.tier <= 4 ? old.tier : 0;
        while (tier < 4 && (uint64_t)v->mv >= (uint64_t)p->threshold_mv[tier] + p->hysteresis_mv)
            tier++;
        while (tier > 0 && (uint64_t)v->mv + p->hysteresis_mv < p->threshold_mv[tier - 1])
            tier--;
    }
    else
        while (tier < 4 && v->mv >= p->threshold_mv[tier])
            tier++;
    r.tier = (uint8_t)tier;
    r.led_mask = (uint8_t)((1u << tier) - 1u);
    if (!tier)
        r.kind = ST_V_LOW;
    return r;
}
