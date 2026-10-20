#include "st_cal_input.h"
#include <string.h>
void st_cal_input_init(st_cal_input *q)
{
    memset(q, 0, sizeof(*q));
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    atomic_init(&q->errors, 0);
    atomic_init(&q->enabled, false);
}
void st_cal_input_error_isr(st_cal_input *q)
{
    if (q && atomic_load_explicit(&q->enabled, memory_order_acquire))
        atomic_fetch_add_explicit(&q->errors, 1, memory_order_relaxed);
}
bool st_cal_input_offer_isr(st_cal_input *q, const st_capture *c)
{
    if (!q || !c || !atomic_load_explicit(&q->enabled, memory_order_acquire))
        return false;
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t >= ST_CAL_INPUT_CAPACITY)
    {
        st_cal_input_error_isr(q);
        return false;
    }
    q->entries[h % ST_CAL_INPUT_CAPACITY] = *c;
    atomic_store_explicit(&q->head, h + 1, memory_order_release);
    return true;
}
static bool collecting(const st_cal_service *s)
{
    return s->workflow.state == ST_CAL_ZERO_COLLECT || s->workflow.state == ST_CAL_LOAD_COLLECT;
}
void st_cal_input_step(st_cal_input *q, st_cal_service *s)
{
    bool run = collecting(s);
    atomic_store_explicit(&q->enabled, run, memory_order_release);
    uint32_t errors = atomic_load_explicit(&q->errors, memory_order_acquire);
    if (run && errors != q->seen_errors)
        st_cal_service_source_error(s);
    q->seen_errors = errors;
    /* Bounded even when the producer keeps receiving DRDY. Old in-flight frames
     * from before a window are discarded by edge timestamp, never re-timestamped. */
    for (unsigned i = 0; i < ST_CAL_INPUT_CAPACITY; i++)
    {
        uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
        uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
        if (t == h)
            break;
        st_capture c = q->entries[t % ST_CAL_INPUT_CAPACITY];
        atomic_store_explicit(&q->tail, t + 1, memory_order_release);
        if (collecting(s) && c.time_us >= s->workflow.collect_us)
            (void)st_cal_service_feed(s, &c);
    }
    atomic_store_explicit(&q->enabled, collecting(s), memory_order_release);
}
