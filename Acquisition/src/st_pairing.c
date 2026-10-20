#include "st_pairing.h"
#include <string.h>
static bool uid_ok(const uint8_t p[12])
{
    unsigned z = 0, f = 0;
    for (unsigned i = 0; i < 12; i++)
    {
        z += p[i] == 0;
        f += p[i] == 255;
    }
    return z != 12 && f != 12;
}
static bool blank(const uint8_t p[96])
{
    for (unsigned i = 0; i < 96; i++)
        if (p[i] != 255)
            return false;
    return true;
}
static bool valid(const uint8_t p[96])
{
    return !memcmp(p, "STNV0001", 8) && st_get32(p + 92) == st_crc32(p, 92);
}
st_nv_status st_pairing_open(st_pairing *p, st_nv_storage storage, const uint8_t uid[12],
                             uint8_t role)
{
    *p = (st_pairing){.storage = storage, .role = role};
    memcpy(p->own_uid, uid, 12);
    p->store.status = ST_NV_UNBOUND;
    if (!storage.verified || !storage.read || !storage.write || !uid_ok(uid) ||
        (role != 1 && role != 2))
        return p->store.status;
    uint8_t bytes[2][96];
    for (unsigned i = 0; i < 2; i++)
        if (!storage.read(storage.ctx, i, bytes[i]))
            return p->store.status = ST_NV_IO_ERROR;
    if (blank(bytes[0]) && blank(bytes[1]))
        return p->store.status = ST_NV_EMPTY;
    int chosen = -1;
    for (unsigned i = 0; i < 2; i++)
        if (valid(bytes[i]))
        {
            p->have_history = true;
            chosen = (int)i;
        }
    if (chosen < 0)
        return p->store.status = ST_NV_DAMAGED;
    const uint8_t *b = bytes[(unsigned)chosen];
    if (memcmp(b + 16, uid, 12) || b[64] != role)
        return p->store.status = ST_NV_FOREIGN;
    st_nv_identity id = {
        .role = role, .epoch_first = st_get32(b + 65), .epoch_last = st_get32(b + 60)};
    memcpy(id.own_uid, uid, 12);
    memcpy(id.peer_uid, b + 28, 12);
    st_nv_bind(&p->store, storage, id);
    return st_nv_open(&p->store);
}
bool st_pairing_initialize_new(st_pairing *p, bool idle, uint32_t ack)
{
    if (!p || !idle || ack != ST_PAIR_NEW_MEDIA_CONFIRM || p->have_history ||
        !p->storage.verified || !p->storage.read || !p->storage.write || !uid_ok(p->own_uid))
        return false;
    uint8_t ff[96], check[96];
    memset(ff, 255, 96);
    for (unsigned i = 0; i < 2; i++)
        if (!p->storage.write(p->storage.ctx, i, ff) ||
            !p->storage.read(p->storage.ctx, i, check) || memcmp(ff, check, 96))
        {
            p->store.status = ST_NV_IO_ERROR;
            return false;
        }
    st_nv_storage storage = p->storage;
    uint8_t uid[12];
    memcpy(uid, p->own_uid, 12);
    uint8_t role = p->role;
    return st_pairing_open(p, storage, uid, role) == ST_NV_EMPTY;
}
bool st_pairing_provision(st_pairing *p, const uint8_t peer[12], unsigned network, bool idle)
{
    if (!p || !peer || !idle || p->store.status != ST_NV_EMPTY || !uid_ok(peer) ||
        !memcmp(p->own_uid, peer, 12) ||
        (p->role == ST_NV_CONTROLLER ? network != 0 : (!network || network > 254)))
        return false;
    uint8_t raw[96];
    for (unsigned i = 0; i < 2; i++)
        if (!p->storage.read(p->storage.ctx, i, raw) || !blank(raw))
            return false;
    st_nv_identity id = {.role = p->role};
    memcpy(id.own_uid, p->own_uid, 12);
    memcpy(id.peer_uid, peer, 12);
    if (p->role == ST_NV_ACQUISITION)
    {
        id.epoch_first = ((uint32_t)network << 24) | 1u;
        id.epoch_last = ((uint32_t)network << 24) | 0xFFFFFFu;
    }
    st_nv_bind(&p->store, p->storage, id);
    bool ok = st_nv_provision(&p->store);
    if (ok)
        p->have_history = true;
    return ok;
}
