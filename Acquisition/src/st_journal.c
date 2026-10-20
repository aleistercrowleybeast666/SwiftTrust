#include "st_journal.h"
#include <string.h>
#define FRAME 256u
static bool blank(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] != 255)
            return false;
    return true;
}
static bool valid(const uint8_t p[FRAME])
{
    if (memcmp(p, "STJNL001", 8) || !st_get64(p + 8) || memcmp(p + 248, "COMMIT01", 8) ||
        st_get32(p + 240) != st_crc32(p, 240))
        return false;
    for (unsigned i = 208; i < 240; i++)
        if (p[i])
            return false;
    for (unsigned i = 244; i < 248; i++)
        if (p[i])
            return false;
    return true;
}
bool st_journal_open(st_journal *j, st_journal_port port)
{
    *j = (st_journal){.port = port};
    memset(j->image, 255, sizeof j->image);
    if (!port.read || !port.program8 || !port.erase || !port.idle || port.region_bytes < 512 ||
        port.region_bytes % FRAME)
        return false;
    uint8_t frame[FRAME];
    uint64_t previous[2] = {0};
    for (unsigned region = 0; region < 2; region++)
    {
        bool ended = false;
        for (uint32_t at = 0; at < port.region_bytes; at += FRAME)
        {
            if (!port.read(port.ctx, region, at, frame, FRAME))
            {
                j->fault = true;
                return false;
            }
            if (blank(frame, FRAME))
            {
                ended = true;
                continue;
            }
            if (ended || !valid(frame))
            {
                j->fault = true;
                return false;
            }
            uint64_t seq = st_get64(frame + 8);
            if (previous[region] && seq != previous[region] + 1)
            {
                j->fault = true;
                return false;
            }
            previous[region] = seq;
            if (seq == j->sequence)
            {
                j->fault = true;
                return false;
            }
            if (seq > j->sequence)
            {
                j->sequence = seq;
                j->region = region;
                j->next = at + FRAME;
                memcpy(j->image, frame + 16, sizeof j->image);
            }
        }
    }
    j->opened = true;
    return true;
}
static bool read_slot(void *ctx, unsigned slot, uint8_t out[ST_NV_BYTES])
{
    st_journal *j = ctx;
    if (!j->opened || j->fault || slot > 1)
        return false;
    memcpy(out, j->image[slot], ST_NV_BYTES);
    return true;
}
static bool write_slot(void *ctx, unsigned slot, const uint8_t in[ST_NV_BYTES])
{
    st_journal *j = ctx;
    if (!j->opened || j->fault || slot > 1 || !j->port.idle(j->port.ctx) ||
        j->sequence == UINT64_MAX)
        return false;
    unsigned region = j->region;
    uint32_t at = j->next;
    if (at == j->port.region_bytes)
    {
        region = 1u - region;
        at = 0;
        if (!j->port.erase(j->port.ctx, region))
        {
            j->fault = true;
            return false;
        }
    }
    uint8_t frame[FRAME] = {0}, check[FRAME];
    memcpy(frame, "STJNL001", 8);
    st_put64(frame + 8, j->sequence + 1);
    memcpy(frame + 16, j->image, sizeof j->image);
    memcpy(frame + 16 + slot * ST_NV_BYTES, in, ST_NV_BYTES);
    st_put32(frame + 240, st_crc32(frame, 240));
    memcpy(frame + 248, "COMMIT01", 8);
    if (!j->port.read(j->port.ctx, region, at, check, FRAME) || !blank(check, FRAME))
    {
        j->fault = true;
        return false;
    }
    /* Header first records an attempted transaction; commit seal last. */
    for (unsigned i = 0; i < FRAME; i += 8)
        if (!j->port.program8(j->port.ctx, region, at + i, frame + i))
        {
            j->fault = true;
            return false;
        }
    if (!j->port.read(j->port.ctx, region, at, check, FRAME) || memcmp(frame, check, FRAME))
    {
        j->fault = true;
        return false;
    }
    memcpy(j->image, frame + 16, sizeof j->image);
    j->sequence++;
    j->region = region;
    j->next = at + FRAME;
    return true;
}
st_nv_storage st_journal_storage(st_journal *j)
{
    return (st_nv_storage){j, read_slot, write_slot, j->opened && !j->fault};
}
