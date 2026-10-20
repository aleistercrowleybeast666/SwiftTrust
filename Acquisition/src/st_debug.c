#include "st_debug.h"
#include <string.h>
void st_debug_init(st_debug *d, st_debug_port p)
{
    memset(d, 0, sizeof(*d));
    d->port = p;
    d->failed = !p.submit || !p.poll;
}
bool st_debug_write(st_debug *d, const uint8_t *p, size_t n, uint32_t now)
{
    if ((uint32_t)(now - d->window_ms) >= 1000)
    {
        d->window_ms = now;
        d->window_bytes = 0;
    }
    if (d->failed || !p || !n || n > 128 || d->window_bytes + n > 256 ||
        n > 512u - (uint16_t)(d->head - d->tail))
    {
        if (d->dropped != UINT32_MAX)
            d->dropped++;
        return false;
    }
    for (size_t i = 0; i < n; i++)
        d->queue[(d->head++) & 511u] = p[i];
    d->window_bytes += (unsigned)n;
    return true;
}
void st_debug_service(st_debug *d)
{
    if (d->failed)
        return;
    if (d->inflight)
    {
        st_async_result r = d->port.poll(d->port.ctx);
        if (r == ST_IO_WAIT)
            return;
        d->inflight = false;
        if (r == ST_IO_ERROR)
        {
            d->failed = true;
            return;
        }
    }
    unsigned n = (uint16_t)(d->head - d->tail);
    if (n > 64)
        n = 64;
    if (!n)
        return;
    for (unsigned i = 0; i < n; i++)
        d->tx[i] = d->queue[(d->tail + i) & 511u];
    st_async_result r = d->port.submit(d->port.ctx, d->tx, n);
    if (r == ST_IO_ERROR)
        d->failed = true;
    else if (r == ST_IO_DONE)
    {
        d->tail = (uint16_t)(d->tail + n);
        d->inflight = true;
    }
}
