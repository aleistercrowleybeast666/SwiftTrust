#include "st_core.h"
#include <limits.h>
#include <string.h>
static int payload_length(uint8_t type)
{
    switch (type)
    {
    case ST_MSG_START:
    case ST_MSG_STOP:
        return 4;
    case ST_MSG_QUERY:
    case ST_MSG_HEARTBEAT:
        return 0;
    case ST_MSG_ACK:
        return 6;
    case ST_MSG_STATUS:
        return 34;
    default:
        return -1;
    }
}
size_t st_packet_encode(const st_packet *p, uint8_t out[ST_PACKET_MAX])
{
    if (payload_length(p->type) != (int)p->length)
        return 0;
    out[0] = 'S';
    out[1] = 'T';
    out[2] = 1;
    out[3] = p->type;
    out[4] = p->length;
    st_put32(out + 5, p->session);
    st_put32(out + 9, p->sequence);
    memcpy(out + 13, p->payload, p->length);
    size_t n = 13u + p->length;
    st_put16(out + n, st_crc16(out, n));
    return n + 2;
}
bool st_packet_decode(st_packet *p, const uint8_t *in, size_t n)
{
    if (n < 15 || n > ST_PACKET_MAX || in[0] != 'S' || in[1] != 'T' || in[2] != 1 ||
        payload_length(in[3]) != (int)in[4] || n != 15u + in[4] ||
        st_get16(in + n - 2) != st_crc16(in, n - 2))
        return false;
    memset(p, 0, sizeof(*p));
    p->type = in[3];
    p->length = in[4];
    p->session = st_get32(in + 5);
    p->sequence = st_get32(in + 9);
    memcpy(p->payload, in + 13, p->length);
    return true;
}
void st_status_pack(uint8_t p[34], const st_status *s)
{
    st_put32(p, s->boot_epoch);
    st_put32(p + 4, s->last_command);
    p[8] = s->state;
    p[9] = s->storage;
    p[10] = s->voltage_flags;
    p[11] = s->quality_flags;
    st_put16(p + 12, s->mv);
    st_put16(p + 14, s->voltage_age_ms);
    st_put32(p + 16, s->rate_millihz);
    st_put32(p + 20, s->last_sample);
    st_put16(p + 24, s->dropped);
    st_put16(p + 26, s->adc_errors);
    st_put32(p + 28, s->durable_sample);
    st_put16(p + 32, s->adc_age_ms);
}
bool st_status_unpack(st_status *s, const uint8_t p[34])
{
    if (p[8] > ST_FAULT || p[9] > 4 || (p[10] & ~3u) || (p[11] & ~31u) || !st_get32(p))
        return false;
    *s = (st_status){.boot_epoch = st_get32(p),
                     .last_command = st_get32(p + 4),
                     .state = p[8],
                     .storage = p[9],
                     .voltage_flags = p[10],
                     .quality_flags = p[11],
                     .mv = st_get16(p + 12),
                     .voltage_age_ms = st_get16(p + 14),
                     .rate_millihz = st_get32(p + 16),
                     .last_sample = st_get32(p + 20),
                     .dropped = st_get16(p + 24),
                     .adc_errors = st_get16(p + 26),
                     .durable_sample = st_get32(p + 28),
                     .adc_age_ms = st_get16(p + 32)};
    return true;
}
void st_server_init(st_server *s, st_logger *l, uint32_t session, uint32_t epoch)
{
    *s = (st_server){.logger = l, .session = session, .boot_epoch = epoch};
}
static uint16_t saturate16(uint32_t v)
{
    return v > 65535 ? 65535 : (uint16_t)v;
}
static size_t status_reply(st_server *s, st_status m, uint64_t now, uint8_t out[ST_PACKET_MAX])
{
    st_logger *l = s->logger;
    st_packet p = {.type = ST_MSG_STATUS,
                   .length = 34,
                   .session = s->session,
                   .sequence = ++s->status_sequence};
    m.boot_epoch = s->boot_epoch;
    m.last_command = s->last_command;
    m.state = (uint8_t)l->state;
    m.storage = l->state == ST_FAULT ? 4 : (l->state == ST_IDLE ? 0 : 1);
    m.last_sample = l->last_sequence;
    m.durable_sample = l->durable_sequence;
    uint32_t drops = atomic_load_explicit(&l->ring.dropped, memory_order_relaxed);
    uint32_t errors = st_logger_error_count(l);
    m.dropped = saturate16(drops);
    m.adc_errors = saturate16(errors);
    m.quality_flags =
        (uint8_t)((m.quality_flags & 1u) | (l->have_sample ? 2u : 0u) |
                  (l->have_durable ? 4u : 0u) | ((drops > 65535 || errors > 65535) ? 8u : 0u) |
                  (l->config.simulated ? 16u : 0u));
    m.rate_millihz = l->config.clock_verified ? l->config.rate_millihz : 0;
    if (!l->config.clock_verified)
        m.quality_flags |= 1u;
    m.adc_age_ms = (!l->have_sample || now < l->last_time_us)
                       ? 65535
                       : saturate16((now - l->last_time_us) / 1000 > 65535
                                        ? 65535
                                        : (uint32_t)((now - l->last_time_us) / 1000));
    st_status_pack(p.payload, &m);
    return st_packet_encode(&p, out);
}
size_t st_server_handle(st_server *s, const uint8_t *in, size_t n, st_status measured, uint64_t now,
                        uint8_t out[ST_PACKET_MAX])
{
    st_packet q;
    if (!s->logger || !s->session || !s->boot_epoch || !st_packet_decode(&q, in, n) ||
        q.session != s->session)
        return 0;
    if (q.type == ST_MSG_QUERY || q.type == ST_MSG_HEARTBEAT)
        return status_reply(s, measured, now, out);
    if (q.type != ST_MSG_START && q.type != ST_MSG_STOP)
        return 0;
    uint8_t result, state = (uint8_t)s->logger->state;
    if (st_get32(q.payload) != s->boot_epoch)
        result = ST_ACK_BOOT;
    else if (!q.sequence || (s->have_command && q.sequence < s->last_command))
        result = ST_ACK_OLD;
    else if (s->have_command && q.sequence == s->last_command)
    {
        if (q.type == s->last_type)
        {
            result = s->last_result;
            state = s->last_state;
        }
        else
            result = ST_ACK_CONFLICT;
    }
    else
    {
        bool accepted =
            q.type == ST_MSG_START ? st_logger_start(s->logger, now) : st_logger_stop(s->logger);
        result = accepted ? ST_ACK_ACCEPTED : ST_ACK_STATE;
        state = (uint8_t)s->logger->state;
        s->last_command = q.sequence;
        s->last_type = q.type;
        s->last_result = result;
        s->last_state = state;
        s->have_command = true;
    }
    st_packet ack = {
        .type = ST_MSG_ACK, .length = 6, .session = s->session, .sequence = q.sequence};
    st_put32(ack.payload, s->boot_epoch);
    ack.payload[4] = result;
    ack.payload[5] = state;
    return st_packet_encode(&ack, out);
}
void st_client_init(st_client *c, uint32_t session, uint32_t retry, unsigned attempts)
{
    *c = (st_client){
        .session = session, .retry_ms = retry, .max_attempts = attempts, .next_command = 1};
}
bool st_client_fresh(const st_client *c, uint32_t now)
{
    return c->known && (uint32_t)(now - c->received_ms) <= 3000;
}
bool st_client_command(st_client *c, uint8_t type, uint32_t now)
{
    if (c->pending || !st_client_fresh(c, now) || !c->next_command || !c->retry_ms ||
        c->retry_ms > 60000 || !c->max_attempts || c->max_attempts > 10)
        return false;
    if (type != ST_MSG_START && type != ST_MSG_STOP)
        return false;
    if (type == ST_MSG_START && c->status.state != ST_IDLE)
        return false;
    if (type == ST_MSG_STOP && c->status.state != ST_CAPTURING && c->status.state != ST_STARTING)
        return false;
    c->request = (st_packet){
        .type = type, .length = 4, .session = c->session, .sequence = c->next_command++};
    st_put32(c->request.payload, c->boot_epoch);
    c->attempts = 0;
    c->pending = true;
    c->transaction = ST_TX_WAIT_ACK;
    c->deadline_ms = now + c->retry_ms * (c->max_attempts + 2u);
    return true;
}
bool st_client_receive(st_client *c, const uint8_t *in, size_t n, uint32_t now)
{
    st_packet p;
    if (!st_packet_decode(&p, in, n) || p.session != c->session)
        return false;
    if (p.type == ST_MSG_STATUS)
    {
        st_status s;
        if (!st_status_unpack(&s, p.payload))
            return false;
        if (c->known)
        {
            if (s.boot_epoch < c->boot_epoch)
                return false;
            if (s.boot_epoch == c->boot_epoch && !st_seq_newer(p.sequence, c->last_status_sequence))
                return false;
        }
        bool reboot = c->known && s.boot_epoch != c->boot_epoch;
        if (reboot && c->pending)
        {
            c->pending = false;
            c->transaction = ST_TX_REBOOT;
        }
        c->known = true;
        c->boot_epoch = s.boot_epoch;
        c->last_status_sequence = p.sequence;
        c->received_ms = now;
        c->status = s;
        if (!c->pending)
            c->next_command = s.last_command == UINT32_MAX ? 0 : s.last_command + 1;
        if (c->pending && s.last_command == c->request.sequence &&
            ((c->request.type == ST_MSG_START && s.state == ST_CAPTURING) ||
             (c->request.type == ST_MSG_STOP && s.state == ST_IDLE)))
        {
            c->pending = false;
            c->transaction = ST_TX_COMPLETE;
        }
        return true;
    }
    if (p.type == ST_MSG_ACK && c->pending && p.sequence == c->request.sequence &&
        st_get32(p.payload) == c->boot_epoch && p.payload[4] <= ST_ACK_CONFLICT &&
        p.payload[5] <= ST_FAULT)
    {
        if (p.payload[4] == ST_ACK_ACCEPTED)
            c->transaction = ST_TX_WAIT_STATE;
        else
        {
            c->pending = false;
            c->transaction = ST_TX_REJECTED;
        }
        return true; /* ACK never refreshes status or voltage freshness. */
    }
    return false;
}
bool st_client_tx_due(st_client *c, uint32_t now, uint8_t out[ST_PACKET_MAX], size_t *n)
{
    if (!c->pending)
        return false;
    if ((uint32_t)(now - c->deadline_ms) < 0x80000000u)
    {
        c->pending = false;
        c->transaction = ST_TX_TIMEOUT;
        return false;
    }
    if (c->transaction == ST_TX_WAIT_STATE || c->attempts >= c->max_attempts)
        return false;
    if (c->attempts && (uint32_t)(now - c->last_send_ms) < c->retry_ms)
        return false;
    *n = st_packet_encode(&c->request, out);
    return *n != 0;
}
void st_client_tx_sent(st_client *c, uint32_t now)
{
    if (c->pending)
    {
        c->attempts++;
        c->last_send_ms = now;
    }
}
uint32_t st_client_voltage_age(const st_client *c, uint32_t now)
{
    if (!c->known || (c->status.voltage_flags & 3u) != 3u || c->status.voltage_age_ms == 65535)
        return UINT32_MAX;
    uint32_t elapsed = now - c->received_ms;
    return elapsed > UINT32_MAX - c->status.voltage_age_ms ? UINT32_MAX
                                                           : elapsed + c->status.voltage_age_ms;
}
