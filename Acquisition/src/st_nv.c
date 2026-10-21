#include "st_nv.h"
#include <string.h>
enum
{
    NV_GENERATION = 8,
    NV_OWN_UID = 16,
    NV_PEER_UID = 28,
    NV_COUNTER = 40,
    NV_PEER_COUNTER = 48,
    NV_EPOCH = 56,
    NV_LIMIT = 60,
    NV_ROLE = 64,
    NV_FIRST = 65,
    NV_RESERVED = 69,
    NV_CRC = 92
};
static bool uid_valid(const uint8_t uid[12])
{
    unsigned zero = 0, ones = 0;
    for (unsigned i = 0; i < 12; i++)
    {
        zero += uid[i] == 0;
        ones += uid[i] == 255;
    }
    return zero != 12 && ones != 12;
}
static bool identity_valid(st_nv_identity id)
{
    if (!uid_valid(id.own_uid) || !uid_valid(id.peer_uid) || !memcmp(id.own_uid, id.peer_uid, 12))
        return false;
    if (id.role == ST_NV_CONTROLLER)
        return !id.epoch_first && !id.epoch_last;
    return id.role == ST_NV_ACQUISITION && id.epoch_first && id.epoch_first <= id.epoch_last;
}
void st_nv_bind(st_nv_store *s, st_nv_storage storage, st_nv_identity id)
{
    *s = (st_nv_store){.storage = storage, .identity = id, .status = ST_NV_UNBOUND};
}
static bool blank(const uint8_t p[ST_NV_BYTES])
{
    for (unsigned i = 0; i < ST_NV_BYTES; i++)
        if (p[i] != 255)
            return false;
    return true;
}
static st_nv_status decode(st_nv_store *s, const uint8_t p[ST_NV_BYTES], st_nv_values *v)
{
    if (blank(p))
        return ST_NV_EMPTY;
    if (memcmp(p, "STNV0001", 8) || st_get32(p + NV_CRC) != st_crc32(p, NV_CRC))
        return ST_NV_DAMAGED;
    for (unsigned i = NV_RESERVED; i < NV_CRC; i++)
        if (p[i])
            return ST_NV_DAMAGED;
    if (memcmp(p + NV_OWN_UID, s->identity.own_uid, 12) ||
        memcmp(p + NV_PEER_UID, s->identity.peer_uid, 12) || p[NV_ROLE] != s->identity.role ||
        st_get32(p + NV_FIRST) != s->identity.epoch_first ||
        st_get32(p + NV_LIMIT) != s->identity.epoch_last)
        return ST_NV_FOREIGN;
    *v = (st_nv_values){st_get64(p + NV_GENERATION), st_get64(p + NV_COUNTER),
                        st_get64(p + NV_PEER_COUNTER), st_get32(p + NV_EPOCH)};
    if (!v->generation)
        return ST_NV_DAMAGED;
    if (s->identity.role == ST_NV_CONTROLLER)
    {
        if (v->peer_counter || v->epoch)
            return ST_NV_DAMAGED;
    }
    else if (v->counter || v->epoch < s->identity.epoch_first - 1 ||
             v->epoch > s->identity.epoch_last)
        return ST_NV_DAMAGED;
    return ST_NV_VALID;
}
static void encode(st_nv_store *s, st_nv_values v, uint8_t p[ST_NV_BYTES])
{
    memset(p, 0, ST_NV_BYTES);
    memcpy(p, "STNV0001", 8);
    st_put64(p + NV_GENERATION, v.generation);
    memcpy(p + NV_OWN_UID, s->identity.own_uid, 12);
    memcpy(p + NV_PEER_UID, s->identity.peer_uid, 12);
    st_put64(p + NV_COUNTER, v.counter);
    st_put64(p + NV_PEER_COUNTER, v.peer_counter);
    st_put32(p + NV_EPOCH, v.epoch);
    st_put32(p + NV_LIMIT, s->identity.epoch_last);
    p[NV_ROLE] = s->identity.role;
    st_put32(p + NV_FIRST, s->identity.epoch_first);
    st_put32(p + NV_CRC, st_crc32(p, NV_CRC));
}
st_nv_status st_nv_open(st_nv_store *s)
{
    if (!s->storage.verified || !s->storage.read || !s->storage.write ||
        !identity_valid(s->identity))
        return s->status = ST_NV_UNBOUND;
    uint8_t bytes[2][ST_NV_BYTES];
    st_nv_values values[2];
    st_nv_status state[2];
    for (unsigned i = 0; i < 2; i++)
    {
        if (!s->storage.read(s->storage.ctx, i, bytes[i]))
            return s->status = ST_NV_IO_ERROR;
        state[i] = decode(s, bytes[i], &values[i]);
    }
    /* Do not roll back to the other slot after an unknown programmed/corrupt slot.
     * A torn write may require service recovery; fail closed rather than reuse IDs. */
    if (state[0] == ST_NV_DAMAGED || state[1] == ST_NV_DAMAGED)
        return s->status = ST_NV_DAMAGED;
    if (state[0] == ST_NV_FOREIGN || state[1] == ST_NV_FOREIGN)
        return s->status = ST_NV_FOREIGN;
    if (state[0] == ST_NV_EMPTY && state[1] == ST_NV_EMPTY)
        return s->status = ST_NV_EMPTY;
    unsigned active = state[0] == ST_NV_VALID ? 0 : 1;
    if (state[0] == ST_NV_VALID && state[1] == ST_NV_VALID)
    {
        if (values[0].generation == values[1].generation)
        {
            if (memcmp(bytes[0], bytes[1], ST_NV_BYTES))
                return s->status = ST_NV_DAMAGED;
        }
        else
        {
            active = values[1].generation > values[0].generation ? 1 : 0;
            st_nv_values newer = values[active], older = values[active ^ 1u];
            if (older.generation == UINT64_MAX || newer.generation != older.generation + 1 ||
                newer.counter < older.counter || newer.peer_counter < older.peer_counter ||
                newer.epoch < older.epoch)
                return s->status = ST_NV_DAMAGED;
        }
    }
    s->active = active;
    s->values = values[active];
    return s->status = ST_NV_VALID;
}
static bool commit(st_nv_store *s, st_nv_values value, unsigned slot)
{
    uint8_t record[ST_NV_BYTES], verify[ST_NV_BYTES];
    encode(s, value, record);
    if (!s->storage.write(s->storage.ctx, slot, record))
    {
        s->status = ST_NV_IO_ERROR;
        return false;
    }
    if (!s->storage.read(s->storage.ctx, slot, verify))
    {
        s->status = ST_NV_IO_ERROR;
        return false;
    }
    if (memcmp(record, verify, ST_NV_BYTES))
    {
        s->status = ST_NV_DAMAGED;
        return false;
    }
    s->active = slot;
    s->values = value;
    s->status = ST_NV_VALID;
    return true;
}
bool st_nv_provision(st_nv_store *s)
{
    if (st_nv_open(s) != ST_NV_EMPTY)
        return false;
    st_nv_values first = {
        .generation = 1,
        .epoch = s->identity.role == ST_NV_ACQUISITION ? s->identity.epoch_first - 1 : 0};
    return commit(s, first, 0);
}
static bool writable(st_nv_store *s)
{
    return s->status == ST_NV_VALID || st_nv_open(s) == ST_NV_VALID;
}
static bool next_counter(void *ctx, uint64_t *out)
{
    st_nv_store *s = ctx;
    if (!out || s->identity.role != ST_NV_CONTROLLER || !writable(s))
        return false;
    st_nv_values v = s->values;
    if (v.counter == UINT64_MAX || v.generation == UINT64_MAX)
    {
        s->status = ST_NV_EXHAUSTED;
        return false;
    }
    v.counter++;
    v.generation++;
    if (!commit(s, v, s->active ^ 1u))
        return false;
    *out = v.counter;
    return true;
}
static bool next_epoch(void *ctx, uint64_t counter, uint32_t *out)
{
    st_nv_store *s = ctx;
    if (!out || s->identity.role != ST_NV_ACQUISITION || !writable(s) || !counter ||
        counter <= s->values.peer_counter)
        return false;
    st_nv_values v = s->values;
    if (v.epoch == s->identity.epoch_last || v.generation == UINT64_MAX)
    {
        s->status = ST_NV_EXHAUSTED;
        return false;
    }
    v.epoch++;
    v.peer_counter = counter;
    v.generation++;
    if (!commit(s, v, s->active ^ 1u))
        return false;
    *out = v.epoch;
    return true;
}
st2_nv_port st_nv_link_port(st_nv_store *s)
{
    return (st2_nv_port){s, next_epoch, next_counter};
}
bool st_nv_server_init(st2_server *server, st_nv_store *s, st_logger *logger, st2_timing timing)
{
    if (s->identity.role != ST_NV_ACQUISITION || st_nv_open(s) != ST_NV_VALID)
        return false;
    st2_server_init(server, logger, st_nv_link_port(s), timing, s->identity.own_uid,
                    s->identity.peer_uid, s->values.peer_counter);
    return true;
}
bool st_nv_client_init(st2_client *client, st_nv_store *s, st2_timing timing)
{
    if (s->identity.role != ST_NV_CONTROLLER || st_nv_open(s) != ST_NV_VALID)
        return false;
    st2_client_init(client, st_nv_link_port(s), timing, s->identity.own_uid, s->identity.peer_uid);
    client->counter = s->values.counter;
    return true;
}
