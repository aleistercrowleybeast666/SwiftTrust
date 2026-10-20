#include "st_link2.h"
#include <string.h>
/* Wire contract: byte0 is TYPE only (no version bits). All multibyte fields
 * use LE helpers; CRC is always the final two bytes. Handshake and short packets
 * intentionally have different layouts; check their exact length before reads. */
static size_t wire_length(const st2_packet *p)
{
    switch (p->type)
    {
    case ST2_STATUS:
        return 12;
    case ST2_START:
    case ST2_STOP:
    case ST2_CAL_QUERY:
        return 9;
    case ST2_CAL_COMMAND:
        return 23;
    case ST2_CAL_REPLY:
        return p->count == 16 ? 26 : 0;
    case ST2_ACK:
    case ST2_DETAILS_QUERY:
        return 10;
    case ST2_DETAILS_REPLY:
        return p->count <= 16 ? 11u + p->count : 0;
    case ST2_HELLO:
        return 35;
    case ST2_HELLO_REPLY:
        return 27;
    case ST2_COMMIT:
        return 15;
    default:
        return 0;
    }
}
static bool fields_valid(const st2_packet *p)
{
    if (p->type == ST2_HELLO)
        return p->counter != 0;
    if (!p->epoch)
        return false;
    if (p->type == ST2_HELLO_REPLY || p->type == ST2_COMMIT)
        return p->counter != 0;
    if (!p->id)
        return false;
    if (p->type == ST2_STATUS)
        return (p->flags & 7u) <= ST_FAULT && (!(p->flags & 32u) || p->mv != 65535);
    if (p->type == ST2_ACK)
        return (p->flags & 7u) <= ST2_FAILED && ((p->flags >> 3) & 7u) <= ST_FAULT;
    if (p->type == ST2_CAL_COMMAND)
    {
        if (p->data[0] > 6)
            return false;
        if (p->data[0] != 2)
        {
            for (unsigned i = 1; i < 14; i++)
                if (p->data[i])
                    return false;
        }
        else if (p->data[1] > 2)
            return false;
    }
    if (p->type == ST2_CAL_REPLY)
        return p->count == 16 && p->fragment >= 0x30 && p->fragment <= 0x33;
    if (p->type == ST2_DETAILS_REPLY)
        return p->count <= 16 && (p->fragment & 15u) <= (p->fragment >> 4);
    return true;
}
size_t st2_encode(const st2_packet *p, uint8_t out[ST2_MAX])
{
    size_t n = wire_length(p);
    if (!n || !fields_valid(p))
        return 0;
    memset(out, 0, n);
    out[0] = p->type;
    if (p->type == ST2_HELLO)
    {
        memcpy(out + 1, p->peer, 12);
        memcpy(out + 13, p->uid, 12);
        st_put64(out + 25, p->counter);
    }
    else if (p->type == ST2_HELLO_REPLY)
    {
        memcpy(out + 1, p->uid, 12);
        st_put64(out + 13, p->counter);
        st_put32(out + 21, p->epoch);
    }
    else
    {
        st_put32(out + 1, p->epoch);
        if (p->type == ST2_COMMIT)
            st_put64(out + 5, p->counter);
        else
        {
            st_put16(out + 5, p->id);
            if (p->type == ST2_STATUS)
            {
                out[7] = p->flags;
                st_put16(out + 8, p->mv);
            }
            if (p->type == ST2_ACK)
                out[7] = p->flags;
            if (p->type == ST2_CAL_COMMAND)
                memcpy(out + 7, p->data, 14);
            if (p->type == ST2_CAL_REPLY)
            {
                out[7] = p->fragment;
                memcpy(out + 8, p->data, 16);
            }
            if (p->type == ST2_DETAILS_QUERY || p->type == ST2_DETAILS_REPLY)
                out[7] = p->section;
            if (p->type == ST2_DETAILS_REPLY)
            {
                out[8] = p->fragment;
                memcpy(out + 9, p->data, p->count);
            }
        }
    }
    st_put16(out + n - 2, st_crc16(out, n - 2));
    return n;
}
bool st2_decode(st2_packet *p, const uint8_t *in, size_t n)
{
    if (!p || !in || n < 9 || n > ST2_MAX || st_get16(in + n - 2) != st_crc16(in, n - 2))
        return false;
    memset(p, 0, sizeof(*p));
    p->type = in[0];
    /* Validate the type's exact length BEFORE accessing variable fields. */
    if (p->type == ST2_DETAILS_REPLY)
    {
        if (n < 11 || n > 27)
            return false;
        p->count = (uint8_t)(n - 11);
    }
    if (p->type == ST2_CAL_REPLY)
        p->count = 16;
    if (wire_length(p) != n)
        return false;
    if (p->type == ST2_HELLO)
    {
        memcpy(p->peer, in + 1, 12);
        memcpy(p->uid, in + 13, 12);
        p->counter = st_get64(in + 25);
    }
    else if (p->type == ST2_HELLO_REPLY)
    {
        memcpy(p->uid, in + 1, 12);
        p->counter = st_get64(in + 13);
        p->epoch = st_get32(in + 21);
    }
    else
    {
        p->epoch = st_get32(in + 1);
        if (p->type == ST2_COMMIT)
            p->counter = st_get64(in + 5);
        else
        {
            p->id = st_get16(in + 5);
            if (p->type == ST2_STATUS)
            {
                p->flags = in[7];
                p->mv = st_get16(in + 8);
            }
            if (p->type == ST2_ACK)
                p->flags = in[7];
            if (p->type == ST2_CAL_COMMAND)
                memcpy(p->data, in + 7, 14);
            if (p->type == ST2_CAL_REPLY)
            {
                p->fragment = in[7];
                memcpy(p->data, in + 8, 16);
            }
            if (p->type == ST2_DETAILS_QUERY || p->type == ST2_DETAILS_REPLY)
                p->section = in[7];
            if (p->type == ST2_DETAILS_REPLY)
            {
                p->fragment = in[8];
                memcpy(p->data, in + 9, p->count);
            }
        }
    }
    return fields_valid(p);
}
bool st2_timing_valid(st2_timing t)
{
    return t.heartbeat_ms >= 5000 && t.heartbeat_ms <= 30000 && t.max_airtime_ms >= 1 &&
           t.max_airtime_ms <= 10000 && t.ack_rto_ms > t.max_airtime_ms && t.ack_rto_ms <= 60000 &&
           t.operation_ms >= t.ack_rto_ms && t.operation_ms <= 600000 && t.attempts >= 1 &&
           t.attempts <= 10;
}
void st2_server_init(st2_server *s, st_logger *l, st2_nv_port nv, st2_timing t,
                     const uint8_t uid[12], const uint8_t peer[12], uint64_t counter)
{
    *s = (st2_server){.logger = l, .nv = nv, .timing = t, .last_counter = counter};
    memcpy(s->uid, uid, 12);
    memcpy(s->peer, peer, 12);
}
void st2_server_attach_calibration(st2_server *s, st2_cal_port p)
{
    s->calibration = p;
}
static void cal_reset(st2_server *s)
{
    if (s->calibration.reset)
        s->calibration.reset(s->calibration.ctx);
    s->cal_cached = false;
    s->cal_owned = false;
}
static size_t cal_reply(st2_server *s, uint8_t *out)
{
    if (!s->cal_cached || s->cal_next >= 4)
        return 0;
    unsigned i = s->cal_next++;
    st2_packet p = {.type = ST2_CAL_REPLY,
                    .epoch = s->epoch,
                    .id = s->last_command,
                    .fragment = (uint8_t)(0x30u | i),
                    .count = 16};
    memcpy(p.data, s->cal_view + i * 16, 16);
    return st2_encode(&p, out);
}
static uint8_t status_flags(st2_server *s, st2_measurement m)
{
    if (m.quality_problem || st_logger_error_count(s->logger) ||
        atomic_load(&s->logger->ring.dropped))
        s->quality_sticky = true;
    return (uint8_t)((unsigned)s->logger->state | ((unsigned)(m.tf & 3u) << 3) |
                     ((m.voltage_fresh && m.mv != 65535) ? 32u : 0u) | (m.adc_fresh ? 64u : 0u) |
                     (s->quality_sticky ? 128u : 0u));
}
static uint8_t ack_flags(st2_server *s, st2_measurement m, uint8_t result)
{
    return (uint8_t)(result | ((unsigned)s->logger->state << 3) | ((unsigned)(m.tf & 3u) << 6));
}
/* Logger progresses independently in its storage task. Upgrade the cached
 * acceptance exactly once on an observed terminal state; retries only read it. */
static void finish_command(st2_server *s, st2_measurement m)
{
    if (!s->have_command || s->last_result != ST2_ACCEPTED)
        return;
    uint8_t result = s->logger->state == ST_FAULT ? ST2_FAILED : ST2_ACCEPTED;
    /* A source fault or explicit reconnect can cancel START before capture.
     * Once the logger drains to IDLE, fail that START instead of leaving it
     * ACCEPTED forever and rejecting every subsequent explicit command. */
    if (s->last_type == ST2_START && s->logger->state == ST_IDLE)
        result = ST2_FAILED;
    if ((s->last_type == ST2_START && s->logger->state == ST_CAPTURING) ||
        (s->last_type == ST2_STOP && s->logger->state == ST_IDLE))
        result = ST2_COMPLETED;
    if (result != ST2_ACCEPTED)
    {
        s->last_result = result;
        s->terminal_pending = true;
        s->last_flags = ack_flags(s, m, result);
    }
}
static size_t send_ack(st2_server *s, uint16_t id, uint8_t flags, uint8_t *out)
{
    st2_packet p = {.type = ST2_ACK, .epoch = s->epoch, .id = id, .flags = flags};
    return st2_encode(&p, out);
}
static size_t send_status(st2_server *s, st2_measurement m, uint32_t now, uint8_t *out)
{
    if (s->status_sequence == 65535)
    {
        s->active = false;
        return 0;
    } /* renew, never wrap */
    uint8_t f = status_flags(s, m);
    st2_packet p = {
        .type = ST2_STATUS, .epoch = s->epoch, .id = ++s->status_sequence, .flags = f, .mv = m.mv};
    s->have_status = true;
    s->previous_flags = f;
    s->previous_mv = m.mv;
    s->last_status_ms = now;
    return st2_encode(&p, out);
}
static void detail_snapshot(st2_server *s, uint64_t now)
{
    st_logger *l = s->logger;
    uint8_t *p = s->snapshot;
    st_put32(p, l->config.clock_verified ? l->config.rate_millihz : 0);
    st_put32(p + 4, l->last_sequence);
    st_put32(p + 8, l->durable_sequence);
    st_put32(p + 12, atomic_load(&l->ring.dropped));
    st_put32(p + 16, st_logger_error_count(l));
    uint64_t age =
        (!l->have_sample || now < l->last_time_us) ? UINT64_MAX : (now - l->last_time_us) / 1000;
    st_put16(p + 20, age >= 65535 ? 65535 : (uint16_t)age);
    p[22] = (uint8_t)((l->have_sample ? 1u : 0u) | (l->have_durable ? 2u : 0u) |
                      (l->config.clock_verified ? 4u : 0u) | (l->config.simulated ? 8u : 0u));
}
static size_t detail_reply(st2_server *s, uint8_t *out)
{
    if (s->detail_next >= 2)
        return 0;
    unsigned index = s->detail_next++;
    st2_packet p = {.type = ST2_DETAILS_REPLY,
                    .epoch = s->epoch,
                    .id = s->detail_id,
                    .section = s->detail_section,
                    .fragment = (uint8_t)(0x10u | index),
                    .count = (uint8_t)(index ? 7 : 16)};
    memcpy(p.data, s->snapshot + index * 16, p.count);
    return st2_encode(&p, out);
}
size_t st2_server_receive(st2_server *s, const uint8_t *in, size_t n, st2_measurement m,
                          uint64_t now, uint8_t out[ST2_MAX])
{
    st2_packet p;
    if (!s->logger || !st2_timing_valid(s->timing) || m.tf > 3 || !st2_decode(&p, in, n))
        return 0;
    if (p.type == ST2_HELLO)
    {
        if (memcmp(p.uid, s->peer, 12) || memcmp(p.peer, s->uid, 12))
            return 0;
        if (!(s->offered && p.counter == s->last_counter))
        {
            uint32_t epoch = 0;
            if (p.counter <= s->last_counter || !s->nv.reserve_epoch ||
                !s->nv.reserve_epoch(s->nv.ctx, p.counter, &epoch) || !epoch)
                return 0;
            cal_reset(s);
            s->last_counter = p.counter;
            s->epoch = epoch;
            s->offered = true;
            s->active = false;
            s->have_command = false;
            s->terminal_pending = false;
            s->have_status = false;
            s->status_sequence = 0;
            s->detail_cached = false;
        }
        st2_packet reply = {.type = ST2_HELLO_REPLY, .epoch = s->epoch, .counter = p.counter};
        memcpy(reply.uid, s->uid, 12);
        return st2_encode(&reply, out);
    }
    if (!s->offered || p.epoch != s->epoch)
        return 0;
    if (p.type == ST2_COMMIT)
    {
        if (p.counter != s->last_counter)
            return 0;
        s->active = true;
        return send_status(s, m, (uint32_t)(now / 1000), out);
    }
    if (!s->active)
        return 0;
    finish_command(s, m);
    if (p.type == ST2_DETAILS_QUERY)
    {
        if (p.section != 1 || (s->detail_cached && p.id < s->detail_id))
            return 0;
        if (!s->detail_cached || p.id != s->detail_id)
        {
            s->detail_id = p.id;
            s->detail_section = p.section;
            s->detail_cached = true;
            detail_snapshot(s, now);
        }
        s->detail_next = 0;
        return detail_reply(s, out);
    }
    if (p.type == ST2_CAL_COMMAND || p.type == ST2_CAL_QUERY)
    {
        if (s->have_command && p.id < s->last_command)
            return send_ack(s, p.id, ack_flags(s, m, ST2_OLD), out);
        if (s->have_command && p.id == s->last_command)
        {
            if (!s->cal_cached || p.type != s->last_type ||
                (p.type == ST2_CAL_COMMAND && memcmp(p.data, s->cal_request, 14)))
                return send_ack(s, p.id, ack_flags(s, m, ST2_CONFLICT), out);
            s->cal_next = 0;
            return cal_reply(s, out);
        }
        if ((s->have_command && s->last_result == ST2_ACCEPTED) || !s->calibration.snapshot ||
            (p.type == ST2_CAL_COMMAND && !s->calibration.request))
            return send_ack(s, p.id, ack_flags(s, m, ST2_STATE_REJECT), out);
        bool ok = true;
        if (p.type == ST2_CAL_COMMAND)
            ok = s->calibration.request(s->calibration.ctx, p.data);
        if (!s->calibration.snapshot(s->calibration.ctx, s->cal_view))
            return send_ack(s, p.id, ack_flags(s, m, ST2_FAILED), out);
        if (!ok)
            s->cal_view[3] = 1; /* rejected; never a durable-save acknowledgement */
        s->cal_last_ms = (uint32_t)(now / 1000);
        s->cal_owned = true;
        s->have_command = true;
        s->last_command = p.id;
        s->last_type = p.type;
        s->last_result = ST2_COMPLETED;
        s->last_flags = ack_flags(s, m, ST2_COMPLETED);
        memcpy(s->cal_request, p.data, 14);
        s->cal_cached = true;
        s->cal_next = 0;
        return cal_reply(s, out);
    }
    if (p.type != ST2_START && p.type != ST2_STOP)
        return 0;
    uint8_t result;
    if (s->have_command && p.id < s->last_command)
        result = ST2_OLD;
    else if (s->have_command && p.id == s->last_command)
    {
        if (p.type != s->last_type)
            result = ST2_CONFLICT;
        else
            return send_ack(s, p.id, s->last_flags, out);
    }
    else if (s->have_command && s->last_result == ST2_ACCEPTED)
        result = ST2_STATE_REJECT;
    else
    {
        bool accepted =
            p.type == ST2_START
                ? ((!s->calibration.locked || !s->calibration.locked(s->calibration.ctx)) &&
                   (!s->calibration.prepare_start ||
                    s->calibration.prepare_start(s->calibration.ctx)) &&
                   st_logger_start(s->logger, now))
                : st_logger_stop(s->logger);
        result = accepted ? ST2_ACCEPTED : ST2_STATE_REJECT;
        s->have_command = true;
        s->last_command = p.id;
        s->last_type = p.type;
        s->cal_cached = false;
        s->last_result = result;
        s->last_flags = ack_flags(s, m, result);
        finish_command(s, m);
        return send_ack(s, p.id, s->last_flags, out);
    }
    return send_ack(s, p.id, ack_flags(s, m, result), out);
}
size_t st2_server_poll(st2_server *s, st2_measurement m, uint64_t now, uint8_t out[ST2_MAX])
{
    if (!s->active || m.tf > 3)
        return 0;
    finish_command(s, m);
    if (s->cal_owned &&
        (uint32_t)((uint32_t)(now / 1000) - s->cal_last_ms) > s->timing.operation_ms)
    {
        if (s->calibration.locked && s->calibration.locked(s->calibration.ctx))
            cal_reset(s); /* lost controller releases a draft, never a committed result */
        else
            s->cal_owned = false;
    }
    if (s->terminal_pending)
    {
        s->terminal_pending = false;
        return send_ack(s, s->last_command, s->last_flags, out);
    }
    if (s->cal_cached && s->cal_next < 4)
        return cal_reply(s, out);
    if (s->detail_cached && s->detail_next < 2)
        return detail_reply(s, out);
    uint8_t f = status_flags(s, m);
    uint32_t ms = (uint32_t)(now / 1000);
    /* Voltage magnitude alone waits for heartbeat; freshness/tier/state events send immediately. */
    if (!s->have_status || f != s->previous_flags ||
        (uint32_t)(ms - s->last_status_ms) >= s->timing.heartbeat_ms)
        return send_status(s, m, ms, out);
    return 0;
}
void st2_client_init(st2_client *c, st2_nv_port nv, st2_timing t, const uint8_t uid[12],
                     const uint8_t peer[12])
{
    *c = (st2_client){.nv = nv, .timing = t};
    memcpy(c->uid, uid, 12);
    memcpy(c->peer, peer, 12);
}
bool st2_client_connect(st2_client *c, uint32_t now)
{
    uint64_t counter = 0;
    if (!st2_timing_valid(c->timing) || !c->nv.reserve_counter ||
        !c->nv.reserve_counter(c->nv.ctx, &counter) || counter <= c->counter)
        return false;
    if (c->pending)
        c->transaction = ST_TX_REBOOT;
    c->counter = counter;
    c->link = ST2_LINK_HELLO;
    c->epoch = 0;
    c->have_status = false;
    c->cal_have_view = false;
    c->cal_seen = 0;
    c->pending = false;
    c->request = (st2_packet){.type = ST2_HELLO, .counter = counter};
    memcpy(c->request.uid, c->uid, 12);
    memcpy(c->request.peer, c->peer, 12);
    c->attempts = 0;
    c->have_sent = false;
    c->waiting_tx_done = false;
    c->started_ms = now;
    return true;
}
bool st2_client_fresh(const st2_client *c, uint32_t now)
{
    return c->link == ST2_LINK_UP && c->have_status &&
           (uint32_t)(now - c->received_ms) <=
               3u * c->timing.heartbeat_ms + c->timing.max_airtime_ms + 1000u;
}
bool st2_client_receive(st2_client *c, const uint8_t *in, size_t n, uint32_t now)
{
    st2_packet p;
    if (!st2_decode(&p, in, n))
        return false;
    if (p.type == ST2_HELLO_REPLY && c->link == ST2_LINK_HELLO && p.counter == c->counter &&
        !memcmp(p.uid, c->peer, 12))
    {
        c->epoch = p.epoch;
        c->request = (st2_packet){.type = ST2_COMMIT, .epoch = p.epoch, .counter = c->counter};
        c->link = ST2_LINK_COMMIT;
        c->attempts = 0;
        c->have_sent = false;
        c->waiting_tx_done = false;
        c->started_ms = now;
        return true;
    }
    if (!c->epoch || p.epoch != c->epoch)
        return false;
    if (p.type == ST2_STATUS && (c->link == ST2_LINK_COMMIT || c->link == ST2_LINK_UP))
    {
        if (c->have_status && p.id <= c->status_sequence)
            return false;
        bool first = c->link == ST2_LINK_COMMIT;
        c->link = ST2_LINK_UP;
        c->have_status = true;
        c->received_ms = now;
        c->status_sequence = p.id;
        c->flags = p.flags;
        c->mv = p.mv;
        if (first)
        {
            c->next_command = 1;
            c->waiting_tx_done = false;
        }
        return true; /* status NEVER completes a transaction */
    }
    if (p.type == ST2_CAL_REPLY && c->link == ST2_LINK_UP && c->pending && p.id == c->request.id &&
        (c->request.type == ST2_CAL_COMMAND || c->request.type == ST2_CAL_QUERY))
    {
        unsigned i = p.fragment & 15u;
        uint8_t bit = (uint8_t)(1u << i);
        if (c->cal_seen & bit)
            return !memcmp(c->cal_view + i * 16, p.data, 16);
        memcpy(c->cal_view + i * 16, p.data, 16);
        c->cal_seen |= bit;
        if (c->cal_seen == 15)
        {
            c->cal_have_view = true;
            c->cal_received_ms = now;
            c->pending = false;
            c->transaction = ST_TX_COMPLETE;
        }
        return true;
    }
    if (p.type == ST2_ACK && c->link == ST2_LINK_UP && c->pending && p.id == c->request.id)
    {
        uint8_t result = p.flags & 7u;
        if (result == ST2_ACCEPTED)
        {
            c->transaction = ST_TX_WAIT_STATE;
            c->attempts = 0;
        }
        else
        {
            c->pending = false;
            c->transaction = result == ST2_COMPLETED ? ST_TX_COMPLETE : ST_TX_REJECTED;
        }
        return true; /* ACK does not refresh telemetry age */
    }
    return false;
}
bool st2_client_command(st2_client *c, uint8_t type, uint32_t now)
{
    if (c->pending || !st2_client_fresh(c, now) || !c->next_command ||
        (type != ST2_START && type != ST2_STOP))
        return false;
    unsigned state = c->flags & 7u;
    if ((type == ST2_START && state != ST_IDLE) ||
        (type == ST2_STOP && state != ST_CAPTURING && state != ST_STARTING))
        return false;
    c->request = (st2_packet){.type = type, .epoch = c->epoch, .id = c->next_command};
    c->next_command = c->next_command == 65535 ? 0 : (uint16_t)(c->next_command + 1);
    c->pending = true;
    c->transaction = ST_TX_WAIT_ACK;
    c->attempts = 0;
    c->have_sent = false;
    c->waiting_tx_done = false;
    c->started_ms = now;
    return true;
}
bool st2_client_cal_command(st2_client *c, const uint8_t data[14], uint32_t now)
{
    /* Read-only polls can be superseded by an explicit user action. A mutation
     * already in flight must complete or hit its bounded timeout first. */
    if (c->pending && c->request.type == ST2_CAL_QUERY && data)
        c->pending = false;
    if (c->pending || !st2_client_fresh(c, now) || !c->next_command ||
        c->timing.operation_ms < c->timing.ack_rto_ms + 4u * c->timing.max_airtime_ms ||
        (data && (c->flags & 7u) != ST_IDLE))
        return false;
    st2_packet p = {
        .type = data ? ST2_CAL_COMMAND : ST2_CAL_QUERY, .epoch = c->epoch, .id = c->next_command};
    if (data)
        memcpy(p.data, data, 14);
    uint8_t wire[ST2_MAX];
    if (!st2_encode(&p, wire))
        return false;
    c->request = p;
    c->next_command = c->next_command == 65535 ? 0 : (uint16_t)(c->next_command + 1);
    c->pending = true;
    c->transaction = ST_TX_WAIT_ACK;
    c->attempts = 0;
    c->have_sent = false;
    c->waiting_tx_done = false;
    c->started_ms = now;
    c->cal_seen = 0;
    c->cal_have_view = false;
    return true;
}
/* now/started/last_send are modulo-2^32 milliseconds. Configured intervals are
 * bounded well below half the timer range. Total deadline also bounds retries
 * when an ACCEPTED reply resets the per-round retry budget. */
bool st2_client_tx_due(st2_client *c, uint32_t now, uint8_t out[ST2_MAX], size_t *n)
{
    if (c->link == ST2_LINK_DOWN || (c->link == ST2_LINK_UP && !c->pending))
        return false;
    if ((uint32_t)(now - c->started_ms) > c->timing.operation_ms)
    {
        if (c->pending)
        {
            c->pending = false;
            c->transaction = ST_TX_TIMEOUT;
        }
        else
            c->link = ST2_LINK_DOWN;
        return false;
    }
    if (c->waiting_tx_done || c->attempts >= c->timing.attempts)
        return false;
    uint32_t reply_window = c->timing.ack_rto_ms;
    if (c->request.type == ST2_CAL_COMMAND || c->request.type == ST2_CAL_QUERY)
        reply_window +=
            3u * c->timing.max_airtime_ms; /* four fragments get one complete reply window */
    if (c->have_sent && (uint32_t)(now - c->last_send_ms) < reply_window + c->retry_jitter_ms)
        return false;
    *n = st2_encode(&c->request, out);
    return *n != 0;
}
void st2_client_tx_accepted(st2_client *c)
{
    c->waiting_tx_done = true;
    c->have_sent = true;
    c->attempts++;
}
void st2_client_tx_done(st2_client *c, uint32_t now, uint32_t jitter)
{
    if (c->waiting_tx_done)
    {
        c->waiting_tx_done = false;
        c->last_send_ms = now;
        c->retry_jitter_ms = jitter % 251u;
    }
}

size_t st2_details_begin(st2_details *d, uint32_t epoch, uint16_t id, uint8_t out[ST2_MAX])
{
    *d = (st2_details){.epoch = epoch, .id = id, .section = 1};
    st2_packet p = {.type = ST2_DETAILS_QUERY, .epoch = epoch, .id = id, .section = 1};
    return st2_encode(&p, out);
}
bool st2_details_receive(st2_details *d, const uint8_t *in, size_t n)
{
    st2_packet p;
    if (!st2_decode(&p, in, n) || p.type != ST2_DETAILS_REPLY || p.epoch != d->epoch ||
        p.id != d->id || p.section != d->section || (p.fragment != 0x10 && p.fragment != 0x11))
        return false;
    unsigned index = p.fragment & 1u;
    uint8_t bit = (uint8_t)(1u << index);
    if (p.count != (index ? 7 : 16))
        return false;
    if (d->seen & bit)
        return !memcmp(d->data + index * 16, p.data, p.count);
    memcpy(d->data + index * 16, p.data, p.count);
    d->seen |= bit;
    d->complete = d->seen == 3;
    return true;
}
