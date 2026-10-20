#include "st_block.h"
#include <string.h>
void st_block_bind(st_block_device *d, st_block_port p, uint32_t sectors, uint32_t erase,
                   uint32_t timeout, bool verified)
{
    *d = (st_block_device){
        .port = p, .sector_count = sectors, .erase_sectors = erase, .timeout_ms = timeout};
    d->bound = verified && sectors && timeout && timeout <= 5000 && p.present && p.ready &&
               p.submit && p.poll && p.abort && p.now_ms && p.yield;
}
bool st_block_present(st_block_device *d)
{
    if (!d || !d->bound)
        return false;
    if (!d->port.present(d->port.ctx))
    {
        d->initialized = false;
        d->fault = true;
        return false;
    }
    return true;
}
bool st_block_initialize(st_block_device *d)
{
    if (!st_block_present(d) || d->fault || d->inflight)
        return false;
    d->initialized = d->port.ready(d->port.ctx);
    return d->initialized;
}
static st_block_result fail(st_block_device *d)
{
    d->fault = true;
    d->initialized = false;
    if (d->inflight)
    {
        d->port.abort(d->port.ctx);
        d->inflight = false;
    }
    return ST_BLOCK_ERROR;
}
static bool expired(st_block_device *d, uint32_t began, unsigned *spins)
{
    return ++*spins > ST_BLOCK_SPIN_LIMIT ||
           (uint32_t)(d->port.now_ms(d->port.ctx) - began) >= d->timeout_ms;
}
static bool wait_ready(st_block_device *d, uint32_t began, unsigned *spins)
{
    while (!d->port.ready(d->port.ctx))
    {
        if (!st_block_present(d) || expired(d, began, spins))
            return false;
        d->port.yield(d->port.ctx);
    }
    return true;
}
st_block_result st_block_transfer(st_block_device *d, bool write, uint32_t first, uint32_t count,
                                  uint8_t *data)
{
    if (!d || !data || !count || count > ST_BLOCK_MAX_SECTORS || first >= d->sector_count ||
        count > d->sector_count - first)
        return ST_BLOCK_PARAMETER;
    if (!st_block_present(d) || !d->initialized || d->fault || d->inflight)
        return ST_BLOCK_NOT_READY;
    uint32_t began = d->port.now_ms(d->port.ctx);
    unsigned spins = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if (!st_block_present(d) || expired(d, began, &spins) || !wait_ready(d, began, &spins))
            return fail(d);
        uint8_t *sector = (uint8_t *)d->bounce;
        if (write)
            memcpy(sector, data + (size_t)i * ST_BLOCK_BYTES, ST_BLOCK_BYTES);
        st_async_result result;
        do
        {
            result = d->port.submit(d->port.ctx, write, sector, first + i);
            if (result == ST_IO_ERROR)
                return fail(d);
            if (result == ST_IO_WAIT)
            {
                if (!st_block_present(d) || expired(d, began, &spins))
                    return fail(d);
                d->port.yield(d->port.ctx);
            }
        } while (result == ST_IO_WAIT);
        d->inflight = true;
        do
        {
            result = d->port.poll(d->port.ctx);
            if (result == ST_IO_ERROR)
                return fail(d);
            if (result == ST_IO_WAIT)
            {
                if (!st_block_present(d) || expired(d, began, &spins))
                    return fail(d);
                d->port.yield(d->port.ctx);
            }
        } while (result == ST_IO_WAIT);
        d->inflight = false;
        if (!st_block_present(d) || expired(d, began, &spins) || !wait_ready(d, began, &spins))
            return fail(d);
        if (!write)
            memcpy(data + (size_t)i * ST_BLOCK_BYTES, sector, ST_BLOCK_BYTES);
    }
    return ST_BLOCK_OK;
}
st_block_result st_block_sync(st_block_device *d)
{
    if (!st_block_present(d) || !d->initialized || d->fault || d->inflight)
        return ST_BLOCK_NOT_READY;
    uint32_t began = d->port.now_ms(d->port.ctx);
    unsigned spins = 0;
    return wait_ready(d, began, &spins) ? ST_BLOCK_OK : fail(d);
}
