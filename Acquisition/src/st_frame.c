#include "st_link2.h"
#include <string.h>
/* R8: marker/version/kind/code, PairID, SessionID, TransactionID (STATUS: seq).
 * All operations use this codec. No R7 frame is embedded or accepted. */
static bool Frame_GetShape(const st2_packet *p, uint8_t *kind, uint8_t *code, size_t *bytes)
{
    *code = 0;
    switch (p->type)
    {
    case ST2_HELLO:
        *kind = ST_WIRE_HELLO;
        *bytes = 32;
        break;
    case ST2_COMMIT:
        *kind = ST_WIRE_HELLO;
        *code = 1;
        *bytes = 8;
        break;
    case ST2_DISCOVER:
        *kind = ST_WIRE_HELLO;
        *code = 2;
        *bytes = 28;
        break;
    case ST2_HELLO_REPLY:
        *kind = ST_WIRE_HELLO_ACK;
        *bytes = 25;
        break;
    case ST2_ADVERTISE:
        *kind = ST_WIRE_HELLO_ACK;
        *code = 2;
        *bytes = 28;
        break;
    case ST2_STATUS:
        *kind = ST_WIRE_STATUS;
        *bytes = 6;
        break;
    case ST2_START:
    case ST2_STOP:
        *kind = ST_WIRE_COMMAND;
        *code = p->type == ST2_START ? 1 : 2;
        *bytes = 1;
        break;
    case ST2_CAL_COMMAND:
        *kind = ST_WIRE_COMMAND;
        *code = 3;
        *bytes = 14;
        break;
    case ST2_SENSOR_COMMAND:
        *kind = ST_WIRE_COMMAND;
        *code = 4;
        *bytes = 2;
        break;
    case ST2_SENSOR_QUERY:
        *kind = ST_WIRE_QUERY;
        *code = 4;
        *bytes = 0;
        break;
    case ST2_SENSOR_REPLY:
        *kind = ST_WIRE_DATA;
        *code = 4;
        *bytes = 12;
        break;
    case ST2_TASK_QUERY:
        *kind = ST_WIRE_QUERY;
        *code = 5;
        *bytes = 0;
        break;
    case ST2_TASK_REPLY:
        *kind = ST_WIRE_DATA;
        *code = 5;
        *bytes = 12;
        break;
    case ST2_STATUS_ACK:
        *kind = ST_WIRE_RESULT;
        *code = 0x7F;
        *bytes = 0;
        break;
    case ST2_ACK:
        *kind = ST_WIRE_RESULT;
        *bytes = 3;
        break;
    case ST2_DETAILS_QUERY:
        *kind = ST_WIRE_QUERY;
        *code = 1;
        *bytes = 0;
        break;
    case ST2_CAL_QUERY:
        *kind = ST_WIRE_QUERY;
        *code = 2;
        *bytes = 0;
        break;
    case ST2_CAL_REPLY:
        *kind = ST_WIRE_DATA;
        *code = 2;
        *bytes = 17;
        break;
    case ST2_DETAILS_REPLY:
        if (p->count > 16)
            return false;
        *kind = ST_WIRE_DATA;
        *code = 1;
        *bytes = 1u + p->count;
        break;
    case ST2_IGN_REQUEST:
        *kind = p->data[0] == IGNITION_QUERY ? ST_WIRE_QUERY : ST_WIRE_COMMAND;
        *code = 0x20;
        *bytes = 24;
        break;
    case ST2_IGN_REPLY:
        *kind = p->data[0] == IGNITION_COMMAND_REPLY ? ST_WIRE_RESULT : ST_WIRE_DATA;
        *code = 0x20;
        *bytes = 26;
        break;
    default:
        return false;
    }
    bool handshake = *kind == ST_WIRE_HELLO || *kind == ST_WIRE_HELLO_ACK;
    if (handshake && p->id)
        return false;
    if (!handshake && (!p->epoch || !p->id))
        return false;
    if (p->type == ST2_COMMIT && !p->epoch)
        return false;
    if (p->type == ST2_STATUS && ((p->flags & 7u) > ST_FAULT || p->task_state > ST_TASK_UNKNOWN ||
                                  ((p->flags & 32u) && p->mv == UINT16_MAX)))
        return false;
    if (p->type == ST2_ACK && ((p->flags & 7u) > ST2_FAILED || ((p->flags >> 3) & 7u) > ST_FAULT))
        return false;
    if (p->type == ST2_CAL_COMMAND && p->data[0] > 6)
        return false;
    if ((p->type == ST2_ACK || p->type == ST2_STATUS) &&
        (p->task_state > ST_TASK_UNKNOWN || p->mode > ST_TASK_OUTPUT))
        return false;
    if (p->type == ST2_STATUS && (p->errors & 0xFEu))
        return false;
    if (p->type == ST2_HELLO_REPLY && p->flags > 2)
        return false;
    if (p->type == ST2_CAL_COMMAND && p->data[0] != 2)
        for (unsigned i = 1; i < 14; i++)
            if (p->data[i])
                return false;
    if (p->type == ST2_CAL_REPLY && (p->fragment < 0x30 || p->fragment > 0x33))
        return false;
    return true;
}
size_t st2_encode(const st2_packet *p, uint8_t out[ST2_MAX])
{
    uint8_t kind, code;
    size_t bytes;
    if (!p || !out || !Frame_GetShape(p, &kind, &code, &bytes))
        return 0;
    size_t total = ST_PROTOCOL_HEADER_BYTES + bytes + 2u;
    if (total > ST2_MAX)
        return 0;
    memset(out, 0, total);
    out[0] = ST_PROTOCOL_MARKER;
    out[1] = ST_PROTOCOL_VERSION;
    out[2] = kind;
    out[3] = code;
    st_put32(out + 4, p->pair_id ? p->pair_id : ST_PROJECT_PAIR_ID);
    st_put32(out + 8, p->epoch);
    st_put32(out + 12, p->id);
    uint8_t *body = out + ST_PROTOCOL_HEADER_BYTES;
    switch (p->type)
    {
    case ST2_HELLO:
        memcpy(body, p->peer, 12);
        memcpy(body + 12, p->uid, 12);
        st_put64(body + 24, p->counter);
        break;
    case ST2_HELLO_REPLY:
        memcpy(body, p->uid, 12);
        st_put64(body + 12, p->counter);
        st_put32(body + 20, st_get32(p->data));
        body[24] = p->flags;
        break;
    case ST2_COMMIT:
        st_put64(body, p->counter);
        break;
    case ST2_DISCOVER:
    case ST2_ADVERTISE:
        memcpy(body, p->uid, 12);
        memcpy(body + 12, p->tag, 16);
        break;
    case ST2_STATUS:
        st_put16(body, p->mv);
        body[2] = p->task_state;
        body[3] = p->mode;
        body[4] = p->flags;
        body[5] = p->errors;
        break;
    case ST2_START:
    case ST2_STOP:
        body[0] = p->mode;
        break;
    case ST2_ACK:
        body[0] = p->flags;
        body[1] = p->task_state;
        body[2] = p->mode;
        break;
    case ST2_CAL_COMMAND:
        memcpy(body, p->data, 14);
        break;
    case ST2_SENSOR_COMMAND:
        memcpy(body, p->data, 2);
        break;
    case ST2_SENSOR_REPLY:
    case ST2_TASK_REPLY:
        memcpy(body, p->data, 12);
        break;
    case ST2_CAL_REPLY:
        body[0] = p->fragment;
        memcpy(body + 1, p->data, 16);
        break;
    case ST2_DETAILS_REPLY:
        body[0] = p->fragment;
        memcpy(body + 1, p->data, p->count);
        break;
    case ST2_IGN_REQUEST:
        memcpy(body, p->data, 8);
        memcpy(body + 8, p->tag, 16);
        break;
    case ST2_IGN_REPLY:
        memcpy(body, p->data, 10);
        memcpy(body + 10, p->tag, 16);
        break;
    default:
        break;
    }
    st_put16(out + total - 2u, st_crc16(out, total - 2u));
    return total;
}
bool st2_decode(st2_packet *p, const uint8_t *in, size_t n)
{
    if (!p || !in || n < 18 || n > ST2_MAX || in[0] != ST_PROTOCOL_MARKER ||
        in[1] != ST_PROTOCOL_VERSION || !st_get32(in + 4) ||
        st_get16(in + n - 2u) != st_crc16(in, n - 2u))
        return false;
    *p = (st2_packet){
        .pair_id = st_get32(in + 4), .epoch = st_get32(in + 8), .id = st_get32(in + 12)};
    uint8_t kind = in[2], code = in[3];
    const uint8_t *body = in + 16;
    size_t bytes = n - 18u;
    if (kind == ST_WIRE_HELLO)
        p->type = code == 0 ? ST2_HELLO : code == 1 ? ST2_COMMIT : code == 2 ? ST2_DISCOVER : 0;
    else if (kind == ST_WIRE_HELLO_ACK)
        p->type = code == 0 ? ST2_HELLO_REPLY : code == 2 ? ST2_ADVERTISE : 0;
    else if (kind == ST_WIRE_STATUS && code == 0)
        p->type = ST2_STATUS;
    else if (kind == ST_WIRE_COMMAND)
        p->type = code == 1      ? ST2_START
                  : code == 2    ? ST2_STOP
                  : code == 3    ? ST2_CAL_COMMAND
                  : code == 4    ? ST2_SENSOR_COMMAND
                  : code == 0x20 ? ST2_IGN_REQUEST
                                 : 0;
    else if (kind == ST_WIRE_RESULT)
        p->type = code == 0      ? ST2_ACK
                  : code == 0x7F ? ST2_STATUS_ACK
                  : code == 0x20 ? ST2_IGN_REPLY
                                 : 0;
    else if (kind == ST_WIRE_QUERY)
        p->type = code == 1      ? ST2_DETAILS_QUERY
                  : code == 2    ? ST2_CAL_QUERY
                  : code == 4    ? ST2_SENSOR_QUERY
                  : code == 5    ? ST2_TASK_QUERY
                  : code == 0x20 ? ST2_IGN_REQUEST
                                 : 0;
    else if (kind == ST_WIRE_DATA)
        p->type = code == 1      ? ST2_DETAILS_REPLY
                  : code == 2    ? ST2_CAL_REPLY
                  : code == 4    ? ST2_SENSOR_REPLY
                  : code == 5    ? ST2_TASK_REPLY
                  : code == 0x20 ? ST2_IGN_REPLY
                                 : 0;
    else
        return false;
    /* Set only shape selectors before the exact length check; no unchecked body reads. */
    if (p->type == ST2_DETAILS_QUERY)
        p->section = 1; /* QUERY code1 identifies the diagnostic section. */
    if (p->type == ST2_DETAILS_REPLY)
    {
        if (bytes < 1 || bytes > 17)
            return false;
        p->count = (uint8_t)(bytes - 1u);
        p->section = 1;
    }
    if (p->type == ST2_CAL_REPLY)
    {
        if (bytes != 17)
            return false;
        p->fragment = body[0];
        p->count = 16;
    }
    if (p->type == ST2_IGN_REQUEST)
    {
        if (bytes != 24)
            return false;
        p->data[0] = body[0];
    }
    if (p->type == ST2_IGN_REPLY)
    {
        if (bytes != 26)
            return false;
        p->data[0] = body[0];
    }
    uint8_t expected_kind, expected_code;
    size_t expected;
    /* Identity/transaction checks run again after decoding actual fields. */
    if (!Frame_GetShape(p, &expected_kind, &expected_code, &expected) || expected != bytes ||
        expected_kind != kind || expected_code != code)
        return false;
    switch (p->type)
    {
    case ST2_HELLO:
        memcpy(p->peer, body, 12);
        memcpy(p->uid, body + 12, 12);
        p->counter = st_get64(body + 24);
        break;
    case ST2_HELLO_REPLY:
        memcpy(p->uid, body, 12);
        p->counter = st_get64(body + 12);
        memcpy(p->data, body + 20, 4);
        p->flags = body[24];
        break;
    case ST2_COMMIT:
        p->counter = st_get64(body);
        break;
    case ST2_DISCOVER:
    case ST2_ADVERTISE:
        memcpy(p->uid, body, 12);
        memcpy(p->tag, body + 12, 16);
        break;
    case ST2_STATUS:
        p->mv = st_get16(body);
        p->task_state = body[2];
        p->mode = body[3];
        p->flags = body[4];
        p->errors = body[5];
        break;
    case ST2_START:
    case ST2_STOP:
        p->mode = body[0];
        break;
    case ST2_ACK:
        p->flags = body[0];
        p->task_state = body[1];
        p->mode = body[2];
        break;
    case ST2_CAL_COMMAND:
        memcpy(p->data, body, 14);
        break;
    case ST2_SENSOR_COMMAND:
        memcpy(p->data, body, 2);
        break;
    case ST2_SENSOR_REPLY:
    case ST2_TASK_REPLY:
        memcpy(p->data, body, 12);
        break;
    case ST2_CAL_REPLY:
        memcpy(p->data, body + 1, 16);
        break;
    case ST2_DETAILS_REPLY:
        p->fragment = body[0];
        memcpy(p->data, body + 1, p->count);
        break;
    case ST2_IGN_REQUEST:
        memcpy(p->data, body, 8);
        memcpy(p->tag, body + 8, 16);
        break;
    case ST2_IGN_REPLY:
        memcpy(p->data, body, 10);
        memcpy(p->tag, body + 10, 16);
        break;
    default:
        break;
    }
    return Frame_GetShape(p, &expected_kind, &expected_code, &expected);
}
