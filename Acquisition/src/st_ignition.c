#include "st_ignition.h"
#include "st_link2.h"
#include "../vendor/tiny_aes/aes.h"
#include <limits.h>
#include <string.h>

#define IGNITION_CMAC_RB 0x87u
#define IGNITION_AUTH_CONTEXT_BYTES 31u
#define IGNITION_PREFIX_REQUEST 24u
#define IGNITION_PREFIX_REPLY 26u
#define IGNITION_SOFTWARE_KNOWN (1u << 6)
#define IGNITION_MAX_RTO_MS 60000u
#define IGNITION_MAX_OPERATION_MS 600000u
#define IGNITION_MAX_ATTEMPTS 10u
#define IGNITION_MAX_JITTER_MS 250u

_Static_assert((unsigned)IGNITION_MAX_BYTES == (unsigned)ST2_MAX, "shared radio payload bound");
_Static_assert((unsigned)IGNITION_REQUEST == (unsigned)ST2_IGN_REQUEST &&
                   (unsigned)IGNITION_REPLY == (unsigned)ST2_IGN_REPLY,
               "shared wire types");
void Ignition_ClearMemory(void *memory, size_t length)
{
    volatile uint8_t *bytes = memory;
    if (bytes)
        while (length--)
            *bytes++ = 0;
}
static void Ignition_ShiftSubkey(uint8_t key[16])
{
    uint8_t carry = (uint8_t)(key[0] >> 7);
    for (unsigned i = 0; i < 15; i++)
        key[i] = (uint8_t)(((unsigned)key[i] << 1) | (key[i + 1] >> 7));
    key[15] = (uint8_t)((unsigned)key[15] << 1);
    if (carry)
        key[15] ^= IGNITION_CMAC_RB;
}
IgnitionCodecResult Ignition_ComputeCmac(const uint8_t key[16], const uint8_t *message,
                                         size_t length, uint8_t tag[16])
{
    if (!key || (!message && length) || !tag)
        return IGNITION_CODEC_INVALID;
    struct AES_ctx aes;
    AES_init_ctx(&aes, key);
    uint8_t subkey[16] = {0}, block[16] = {0};
    AES_ECB_encrypt(&aes, subkey);
    Ignition_ShiftSubkey(subkey);
    size_t blocks = length / 16u + (length % 16u != 0);
    if (!blocks)
        blocks = 1;
    for (size_t index = 0; index + 1 < blocks; index++)
    {
        for (unsigned i = 0; i < 16; i++)
            block[i] ^= message[index * 16u + i];
        AES_ECB_encrypt(&aes, block);
    }
    size_t tail = length ? length - (blocks - 1u) * 16u : 0;
    if (tail != 16)
        Ignition_ShiftSubkey(subkey);
    for (unsigned i = 0; i < 16; i++)
    {
        uint8_t value = i < tail ? message[(blocks - 1u) * 16u + i] : (i == tail ? 0x80u : 0u);
        block[i] ^= value ^ subkey[i];
    }
    AES_ECB_encrypt(&aes, block);
    memcpy(tag, block, 16);
    Ignition_ClearMemory(&aes, sizeof aes);
    Ignition_ClearMemory(subkey, sizeof subkey);
    Ignition_ClearMemory(block, sizeof block);
    return IGNITION_CODEC_OK;
}
static bool Ignition_FieldsValid(const IgnitionPacket *packet)
{
    if (!packet || !packet->epoch || !packet->id)
        return false;
    const uint8_t *data = packet->data;
    if (packet->type == IGNITION_REQUEST)
    {
        uint32_t arm = st_get32(data + 2);
        uint16_t profile = st_get16(data + 6);
        if (data[0] == IGNITION_QUERY)
            return data[1] <= 1 && !profile;
        if (data[0] == IGNITION_DISARM)
            return !data[1] && !arm && !profile;
        if (data[0] != IGNITION_ARM && data[0] != IGNITION_FIRE)
            return false;
        return data[1] >= 1 && data[1] <= 2 && profile &&
               (data[0] == IGNITION_ARM ? !arm : arm != 0);
    }
    if (packet->type != IGNITION_REPLY || data[0] > IGNITION_DETAIL_REPLY)
        return false;
    if (data[0] == IGNITION_DETAIL_REPLY)
    {
        uint16_t known = st_get16(data + 1), pass = st_get16(data + 3);
        return !(known & 0xE000u) && !(pass & (uint16_t)~known) && !(data[9] & 0xFCu);
    }
    return data[1] <= IGNITION_SNAPSHOT && data[2] <= IGNITION_BOOT_LOCKOUT &&
           data[3] <= IGNITION_REASON_HISTORY;
}
static IgnitionCodecResult Ignition_MakeTag(const IgnitionAuth *auth, uint8_t type,
                                            const uint8_t *prefix, size_t length, uint8_t tag[16])
{
    uint8_t message[64];
    if (!auth || !auth->key_bound)
        return IGNITION_CODEC_KEY_UNBOUND;
    memcpy(message, "STIGN8", 6);
    message[6] = type == IGNITION_REQUEST ? 0 : 1;
    memcpy(message + 7, auth->acquisition_uid, 12);
    memcpy(message + 19, auth->controller_uid, 12);
    memcpy(message + IGNITION_AUTH_CONTEXT_BYTES, prefix, length);
    IgnitionCodecResult result =
        Ignition_ComputeCmac(auth->key, message, IGNITION_AUTH_CONTEXT_BYTES + length, tag);
    Ignition_ClearMemory(message, sizeof message);
    return result;
}
IgnitionCodecResult Ignition_Encode(const IgnitionAuth *auth, const IgnitionPacket *packet,
                                    uint8_t out[IGNITION_MAX_BYTES], size_t *length)
{
    if (!out || !length || !Ignition_FieldsValid(packet))
        return IGNITION_CODEC_INVALID;
    *length = 0;
    size_t prefix =
        packet->type == IGNITION_REQUEST ? IGNITION_PREFIX_REQUEST : IGNITION_PREFIX_REPLY;
    st2_packet frame = {.type = packet->type, .epoch = packet->epoch, .id = packet->id};
    memcpy(frame.data, packet->data, packet->type == IGNITION_REQUEST ? 8u : 10u);
    if (!st2_encode(&frame, out))
        return IGNITION_CODEC_FORMAT;
    IgnitionCodecResult result = Ignition_MakeTag(auth, packet->type, out, prefix, out + prefix);
    if (result != IGNITION_CODEC_OK)
        return result;
    st_put16(out + prefix + 16u, st_crc16(out, prefix + 16u));
    *length = prefix + 18u;
    return IGNITION_CODEC_OK;
}
IgnitionCodecResult Ignition_Decode(const IgnitionAuth *auth, uint8_t expected_type,
                                    const uint8_t *wire, size_t length, IgnitionPacket *packet)
{
    if (!auth || !wire || !packet ||
        (expected_type != IGNITION_REQUEST && expected_type != IGNITION_REPLY))
        return IGNITION_CODEC_INVALID;
    size_t prefix =
        expected_type == IGNITION_REQUEST ? IGNITION_PREFIX_REQUEST : IGNITION_PREFIX_REPLY;
    st2_packet frame;
    if (length != prefix + 18u || !st2_decode(&frame, wire, length) ||
        frame.type != expected_type || frame.pair_id != ST_PROJECT_PAIR_ID)
        return IGNITION_CODEC_FORMAT;
    uint8_t tag[16], difference = 0;
    IgnitionCodecResult result = Ignition_MakeTag(auth, expected_type, wire, prefix, tag);
    if (result != IGNITION_CODEC_OK)
        return result;
    for (unsigned i = 0; i < 16; i++)
        difference |= tag[i] ^ wire[prefix + i];
    Ignition_ClearMemory(tag, sizeof tag);
    if (difference)
        return IGNITION_CODEC_AUTH;
    *packet = (IgnitionPacket){.type = expected_type, .epoch = frame.epoch, .id = frame.id};
    memcpy(packet->data, frame.data, expected_type == IGNITION_REQUEST ? 8u : 10u);
    return Ignition_FieldsValid(packet) ? IGNITION_CODEC_OK : IGNITION_CODEC_FORMAT;
}
static bool Ignition_IsUidValid(const uint8_t uid[12])
{
    unsigned zero = 0, ff = 0;
    for (unsigned i = 0; i < 12; i++)
    {
        zero += uid[i] == 0;
        ff += uid[i] == 255;
    }
    return zero != 12 && ff != 12;
}
static IgnitionKeyResult Ignition_SetKey(IgnitionAuth *auth, uint32_t epoch, const uint8_t key[16])
{
    if (!auth)
        return IGNITION_KEY_INVALID;
    if (epoch)
        return IGNITION_KEY_BUSY;
    if (key)
    {
        if (!Ignition_IsUidValid(auth->acquisition_uid) ||
            !Ignition_IsUidValid(auth->controller_uid) ||
            !memcmp(auth->acquisition_uid, auth->controller_uid, 12))
            return IGNITION_KEY_INVALID;
        unsigned zero = 0, ff = 0;
        for (unsigned i = 0; i < 16; i++)
        {
            zero += key[i] == 0;
            ff += key[i] == 255;
        }
        if (zero == 16 || ff == 16)
            return IGNITION_KEY_INVALID;
    }
    uint8_t copied[16] = {0};
    if (key)
        memcpy(copied, key, 16);
    Ignition_ClearMemory(auth->key, sizeof auth->key);
    auth->key_bound = key != NULL;
    if (key)
        memcpy(auth->key, copied, 16);
    Ignition_ClearMemory(copied, sizeof copied);
    return IGNITION_KEY_OK;
}
void IgnitionServer_Init(IgnitionServer *server, const uint8_t acquisition[12],
                         const uint8_t controller[12])
{
    if (!server)
        return;
    memset(server, 0, sizeof *server);
    if (!acquisition || !controller)
        return;
    memcpy(server->auth.acquisition_uid, acquisition, 12);
    memcpy(server->auth.controller_uid, controller, 12);
}
IgnitionKeyResult IgnitionServer_SetKey(IgnitionServer *server, const uint8_t key[16])
{
    return server ? Ignition_SetKey(&server->auth, server->epoch, key) : IGNITION_KEY_INVALID;
}
IgnitionSessionResult IgnitionServer_SetSession(IgnitionServer *server, uint32_t epoch, bool active)
{
    if (!server || (active && !epoch))
        return IGNITION_SESSION_INVALID;
    if (active && epoch == server->epoch)
        return IGNITION_SESSION_OK;
    if (server->output_bound)
    {
        IgnitionOutputResult stopped = server->output.stop(server->output.ctx);
        server->state = stopped == IGNITION_OUTPUT_OK ? IGNITION_SAFE : IGNITION_FAULT;
        server->reason =
            stopped == IGNITION_OUTPUT_OK ? IGNITION_REASON_NONE : IGNITION_REASON_OUTPUT;
        server->arm_id = 0;
    }
    if (server->epoch)
        server->retired_epoch = server->epoch;
    server->epoch = 0;
    server->query_cached = false;
    server->query_count = server->query_next = 0;
    server->last_command = server->last_query = 0;
    memset(server->commands, 0, sizeof server->commands);
    if (!active)
        return IGNITION_SESSION_OK;
    if (epoch <= server->retired_epoch)
        return IGNITION_SESSION_RETIRED;
    server->epoch = epoch;
    return IGNITION_SESSION_OK;
}
static size_t IgnitionServer_MakeReply(IgnitionServer *server, uint32_t id, uint8_t kind,
                                       uint8_t result, uint8_t out[IGNITION_MAX_BYTES])
{
    IgnitionPacket reply = {.type = IGNITION_REPLY, .epoch = server->epoch, .id = id};
    reply.data[0] = kind;
    reply.data[1] = result;
    reply.data[2] = server->output_bound ? server->state : IGNITION_DISABLED;
    reply.data[3] = result == IGNITION_RESULT_UNKNOWN
                        ? IGNITION_REASON_HISTORY
                        : (server->output_bound ? server->reason : IGNITION_REASON_UNBOUND);
    if (server->output_bound)
    {
        st_put32(reply.data + 4, server->arm_id);
        if (server->state == IGNITION_ARMED || server->state == IGNITION_FIRING)
        {
            uint32_t now = server->output.now_ms(server->output.ctx);
            int32_t remaining = (int32_t)(server->deadline_ms - now);
            st_put16(reply.data + 8, remaining > 0 ? (uint16_t)remaining : 0);
        }
    }
    size_t length = 0;
    (void)Ignition_Encode(&server->auth, &reply, out, &length);
    return length;
}
bool IgnitionServer_IsBusy(const IgnitionServer *server)
{
    return server && server->output_bound &&
           (server->state == IGNITION_ARMED || server->state == IGNITION_FIRING);
}
IgnitionOutputResult IgnitionServer_BindOutput(IgnitionServer *server, IgnitionOutputPort port)
{
    if (!server || server->epoch || !port.now_ms || !port.allowed || !port.start || !port.stop ||
        !port.running)
        return IGNITION_OUTPUT_INVALID;
    IgnitionOutputResult result = port.stop(port.ctx);
    if (result != IGNITION_OUTPUT_OK || port.running(port.ctx))
        return IGNITION_OUTPUT_FAULT;
    server->output = port;
    server->output_bound = true;
    server->state = IGNITION_SAFE;
    return IGNITION_OUTPUT_OK;
}
void IgnitionServer_Service(IgnitionServer *server)
{
    if (!server || !server->output_bound)
        return;
    uint32_t now = server->output.now_ms(server->output.ctx);
    if (server->state == IGNITION_ARMED &&
        ((int32_t)(now - server->deadline_ms) >= 0 ||
         !server->output.allowed(server->output.ctx, server->mode)))
    {
        server->state = IGNITION_SAFE;
        server->reason = IGNITION_REASON_LEASE;
        server->arm_id = 0;
    }
    if (server->state == IGNITION_FIRING)
    {
        bool ended = !server->output.running(server->output.ctx);
        bool local_fault = !server->output.allowed(server->output.ctx, server->mode);
        bool overdue = (int32_t)(now - server->deadline_ms) >= 0;
        if (ended || local_fault || overdue)
        {
            IgnitionOutputResult result = server->output.stop(server->output.ctx);
            server->state = result == IGNITION_OUTPUT_OK ? IGNITION_LOCKOUT : IGNITION_FAULT;
            server->reason = result != IGNITION_OUTPUT_OK
                                 ? IGNITION_REASON_OUTPUT
                                 : (local_fault ? IGNITION_REASON_LOCAL : IGNITION_REASON_NONE);
            server->arm_id = 0;
            IgnitionCommandCache *fire = &server->commands[IGNITION_FIRE - 1u];
            if (fire->valid)
                (void)IgnitionServer_MakeReply(server, fire->id, IGNITION_COMMAND_REPLY,
                                               result == IGNITION_OUTPUT_OK && !local_fault
                                                   ? IGNITION_OUTPUT_ENDED
                                                   : IGNITION_FAILED,
                                               fire->reply);
        }
    }
}
static uint8_t IgnitionServer_Execute(IgnitionServer *server, const IgnitionPacket *request)
{
    uint8_t action = request->data[0], mode = request->data[1];
    if (!server->output_bound)
        return action == IGNITION_DISARM ? IGNITION_FAILED : IGNITION_REJECTED;
    server->reason = IGNITION_REASON_NONE;
    if (action == IGNITION_DISARM)
    {
        IgnitionOutputResult result = server->output.stop(server->output.ctx);
        server->arm_id = 0;
        server->state = result == IGNITION_OUTPUT_OK ? IGNITION_SAFE : IGNITION_FAULT;
        server->reason =
            result == IGNITION_OUTPUT_OK ? IGNITION_REASON_NONE : IGNITION_REASON_OUTPUT;
        return result == IGNITION_OUTPUT_OK ? IGNITION_DISARMED : IGNITION_FAILED;
    }
    if (st_get16(request->data + 6) != IGNITION_SIGNAL_PROFILE)
        server->reason = IGNITION_REASON_PROFILE;
    else if (!server->output.allowed(server->output.ctx, mode))
        server->reason = IGNITION_REASON_LOCAL;
    else if (action == IGNITION_ARM && server->state == IGNITION_SAFE)
    {
        server->mode = mode;
        server->arm_id = request->id; /* Unique within the durable, authenticated epoch. */
        server->deadline_ms = server->output.now_ms(server->output.ctx) + IGNITION_ARM_MS;
        server->state = IGNITION_ARMED;
        return IGNITION_ARM_GRANTED;
    }
    else if (action == IGNITION_FIRE && server->state == IGNITION_ARMED && mode == server->mode &&
             st_get32(request->data + 2) == server->arm_id)
    {
        /* Consume authorization before touching the peripheral; never restart on retry. */
        server->arm_id = 0;
        server->state = IGNITION_LOCKOUT;
        if (server->output.start(server->output.ctx) == IGNITION_OUTPUT_OK)
        {
            server->deadline_ms =
                server->output.now_ms(server->output.ctx) + IGNITION_PULSE_MS + 1u;
            server->state = IGNITION_FIRING;
            return IGNITION_FIRE_ACCEPTED;
        }
        (void)server->output.stop(server->output.ctx);
        server->state = IGNITION_FAULT;
        server->reason = IGNITION_REASON_OUTPUT;
        return IGNITION_FAILED;
    }
    else
        server->reason = IGNITION_REASON_STATE;
    return IGNITION_REJECTED;
}
size_t IgnitionServer_Receive(IgnitionServer *server, const uint8_t *wire, size_t length,
                              uint8_t out[IGNITION_MAX_BYTES])
{
    IgnitionPacket request;
    if (!server || !out || !server->epoch ||
        Ignition_Decode(&server->auth, IGNITION_REQUEST, wire, length, &request) !=
            IGNITION_CODEC_OK ||
        request.epoch != server->epoch)
        return 0;
    IgnitionServer_Service(server);
    if (request.data[0] == IGNITION_QUERY)
    {
        if (server->query_cached && request.id == server->last_query)
        {
            if (memcmp(request.data, server->query_request, 8))
                return IgnitionServer_MakeReply(server, request.id, IGNITION_QUERY_REPLY,
                                                IGNITION_CONFLICT, out);
            server->query_next = 1;
            memcpy(out, server->query_reply[0], IGNITION_REPLY_BYTES);
            return IGNITION_REPLY_BYTES;
        }
        if (request.id <= server->last_query)
            return 0;
        uint32_t target = st_get32(request.data + 2);
        uint8_t result =
            target ? (target > server->last_command ? IGNITION_NOT_SEEN : IGNITION_RESULT_UNKNOWN)
                   : IGNITION_SNAPSHOT;
        for (unsigned i = 0; i < 3; i++)
            if (target && server->commands[i].valid && server->commands[i].id == target)
                result = server->commands[i].reply[17];
        server->last_query = request.id;
        memcpy(server->query_request, request.data, 8);
        server->query_cached = true;
        server->query_count = request.data[1] ? 2 : 1;
        server->query_next = 1;
        (void)IgnitionServer_MakeReply(server, request.id, IGNITION_QUERY_REPLY, result,
                                       server->query_reply[0]);
        if (server->query_count == 2)
        {
            IgnitionPacket detail = {
                .type = IGNITION_REPLY, .epoch = server->epoch, .id = request.id};
            detail.data[0] = IGNITION_DETAIL_REPLY;
            /* Only software facts are known. No guessed voltage/profile/output feedback. */
            st_put16(detail.data + 1, IGNITION_SOFTWARE_KNOWN);
            st_put16(detail.data + 3, IGNITION_SOFTWARE_KNOWN);
            st_put16(detail.data + 5, UINT16_MAX);
            if (server->output_bound)
            {
                st_put16(detail.data + 1, IGNITION_SOFTWARE_KNOWN | (1u << 4) | (1u << 5));
                st_put16(detail.data + 3, IGNITION_SOFTWARE_KNOWN | (1u << 4) | (1u << 5));
                st_put16(detail.data + 7, IGNITION_SIGNAL_PROFILE);
                detail.data[9] = 3; /* Both modes, MCU signal only; no external feedback. */
            }
            size_t encoded = 0;
            (void)Ignition_Encode(&server->auth, &detail, server->query_reply[1], &encoded);
        }
        memcpy(out, server->query_reply[0], IGNITION_REPLY_BYTES);
        return IGNITION_REPLY_BYTES;
    }
    IgnitionCommandCache *cache = &server->commands[request.data[0] - 1u];
    for (unsigned i = 0; i < 3; i++)
        if (server->commands[i].valid && server->commands[i].id == request.id)
        {
            if (cache != &server->commands[i] || memcmp(cache->request, request.data, 8))
                return IgnitionServer_MakeReply(server, request.id, IGNITION_COMMAND_REPLY,
                                                IGNITION_CONFLICT, out);
            memcpy(out, cache->reply, IGNITION_REPLY_BYTES);
            return IGNITION_REPLY_BYTES;
        }
    if (request.id <= server->last_command)
        return IgnitionServer_MakeReply(server, request.id, IGNITION_COMMAND_REPLY, IGNITION_OLD_ID,
                                        out);
    server->last_command = request.id;
    if (request.data[0] == IGNITION_DISARM)
        server->query_next = server->query_count; /* Cancel unsent optional detail. */
    cache->id = request.id;
    cache->valid = true;
    memcpy(cache->request, request.data, 8);
    uint8_t result = IgnitionServer_Execute(server, &request);
    size_t encoded =
        IgnitionServer_MakeReply(server, request.id, IGNITION_COMMAND_REPLY, result, cache->reply);
    memcpy(out, cache->reply, encoded);
    return encoded;
}
size_t IgnitionServer_Poll(IgnitionServer *server, uint8_t out[IGNITION_MAX_BYTES])
{
    if (!server || !out || !server->epoch || server->query_next >= server->query_count)
        return 0;
    memcpy(out, server->query_reply[server->query_next++], IGNITION_REPLY_BYTES);
    return IGNITION_REPLY_BYTES;
}
void IgnitionClient_Init(IgnitionClient *client, const uint8_t acquisition[12],
                         const uint8_t controller[12], uint32_t rto, uint32_t operation,
                         unsigned attempts)
{
    if (!client)
        return;
    memset(client, 0, sizeof *client);
    if (!acquisition || !controller)
        return;
    memcpy(client->auth.acquisition_uid, acquisition, 12);
    memcpy(client->auth.controller_uid, controller, 12);
    client->ack_rto_ms = rto;
    client->operation_ms = operation;
    client->max_attempts = attempts;
}
IgnitionKeyResult IgnitionClient_SetKey(IgnitionClient *client, const uint8_t key[16])
{
    return client ? Ignition_SetKey(&client->auth, client->epoch, key) : IGNITION_KEY_INVALID;
}
void IgnitionClient_Fail(IgnitionClient *client)
{
    if (!client)
        return;
    if (client->pending)
        client->operation = IGNITION_OPERATION_UNKNOWN;
    client->pending = client->waiting_tx = client->have_grant = false;
    client->have_reply = client->have_details = false;
}
IgnitionSessionResult IgnitionClient_SetSession(IgnitionClient *client, uint32_t epoch, bool active)
{
    if (!client || (active && !epoch))
        return IGNITION_SESSION_INVALID;
    if (active && epoch == client->epoch)
        return IGNITION_SESSION_OK;
    if (client->epoch)
        client->retired_epoch = client->epoch;
    client->epoch = 0;
    IgnitionClient_Fail(client);
    if (!active)
        return IGNITION_SESSION_OK;
    if (epoch <= client->retired_epoch)
        return IGNITION_SESSION_RETIRED;
    client->epoch = epoch;
    client->next_command = 1;
    client->profile_id = 0;
    client->request_result = IGNITION_REQUEST_OK;
    return IGNITION_SESSION_OK;
}
static IgnitionRequestResult IgnitionClient_Begin(IgnitionClient *client, uint8_t action,
                                                  uint8_t mode, uint32_t arm, uint16_t profile,
                                                  uint32_t now)
{
    if (!client || !client->ack_rto_ms || client->ack_rto_ms > IGNITION_MAX_RTO_MS ||
        client->operation_ms < client->ack_rto_ms ||
        client->operation_ms > IGNITION_MAX_OPERATION_MS || !client->max_attempts ||
        client->max_attempts > IGNITION_MAX_ATTEMPTS)
        return IGNITION_REQUEST_INVALID;
    if (!client->auth.key_bound)
        return client->request_result = IGNITION_REQUEST_KEY_UNBOUND;
    if (!client->epoch)
        return client->request_result = IGNITION_REQUEST_LINK_DOWN;
    if (client->pending && action != IGNITION_DISARM)
        return client->request_result = IGNITION_REQUEST_BUSY;
    uint32_t *next =
        client->transaction_source ? client->transaction_source : &client->next_command;
    if (!*next)
        return client->request_result = IGNITION_REQUEST_EXHAUSTED;
    IgnitionPacket request = {.type = IGNITION_REQUEST, .epoch = client->epoch, .id = *next};
    request.data[0] = action;
    request.data[1] = mode;
    st_put32(request.data + 2, arm);
    st_put16(request.data + 6, profile);
    uint8_t wire[IGNITION_MAX_BYTES];
    size_t length = 0;
    if (Ignition_Encode(&client->auth, &request, wire, &length) != IGNITION_CODEC_OK)
        return client->request_result = IGNITION_REQUEST_INVALID;
    *next = *next == UINT32_MAX ? 0 : *next + 1u;
    memcpy(client->request, wire, length);
    client->started_ms = now;
    client->attempts = client->seen = 0;
    client->pending = true;
    client->waiting_tx = client->sent = false;
    client->operation = IGNITION_OPERATION_WAITING;
    if (action == IGNITION_DISARM)
        client->have_grant = false;
    return client->request_result = IGNITION_REQUEST_OK;
}
IgnitionRequestResult IgnitionClient_Query(IgnitionClient *client, bool details, uint32_t target,
                                           uint32_t now)
{
    return IgnitionClient_Begin(client, IGNITION_QUERY, details ? 1 : 0, target, 0, now);
}
IgnitionRequestResult IgnitionClient_RequestArm(IgnitionClient *client, uint8_t mode,
                                                uint16_t profile, uint32_t now)
{
    if (!profile)
        return client ? (client->request_result = IGNITION_REQUEST_NO_PROFILE)
                      : IGNITION_REQUEST_INVALID;
    return IgnitionClient_Begin(client, IGNITION_ARM, mode, 0, profile, now);
}
IgnitionRequestResult IgnitionClient_RequestFire(IgnitionClient *client, uint32_t now)
{
    if (!client || !client->have_grant || (int32_t)(now - client->grant_deadline_ms) >= 0)
        return client ? (client->request_result = IGNITION_REQUEST_NOT_ARMED)
                      : IGNITION_REQUEST_INVALID;
    IgnitionRequestResult result = IgnitionClient_Begin(client, IGNITION_FIRE, client->mode,
                                                        client->arm_id, client->profile_id, now);
    if (result == IGNITION_REQUEST_OK)
        client->have_grant = false;
    return result;
}
IgnitionRequestResult IgnitionClient_RequestDisarm(IgnitionClient *client, uint32_t now)
{
    return IgnitionClient_Begin(client, IGNITION_DISARM, 0, 0, 0, now);
}
bool IgnitionClient_RequestMatches(const IgnitionClient *client, const uint8_t *wire, size_t length)
{
    return client && wire && client->pending && length == IGNITION_REQUEST_BYTES &&
           !memcmp(client->request, wire, length);
}
bool IgnitionClient_TxDue(IgnitionClient *client, uint32_t now, uint8_t out[IGNITION_MAX_BYTES],
                          size_t *length)
{
    if (!client || !out || !length)
        return false;
    *length = 0;
    if (!client->pending)
        return false;
    bool fire = client->request[IGNITION_ACTION_OFFSET] == IGNITION_FIRE;
    uint32_t window = client->ack_rto_ms + client->jitter_ms;
    if (client->request[IGNITION_ACTION_OFFSET] == IGNITION_QUERY &&
        client->request[IGNITION_MODE_OFFSET])
        window += client->ack_rto_ms; /* Two 35B replies, bounded by configured single-reply RTO. */
    if ((fire && (int32_t)(now - client->grant_deadline_ms) >= 0) ||
        (uint32_t)(now - client->started_ms) > client->operation_ms ||
        (!client->waiting_tx && client->sent && (uint32_t)(now - client->last_done_ms) >= window &&
         (fire || client->attempts >= client->max_attempts)))
    {
        client->pending = false;
        client->waiting_tx = false;
        client->operation = IGNITION_OPERATION_UNKNOWN;
        client->have_grant = false;
        return false;
    }
    if (client->waiting_tx ||
        (client->sent && (fire || (uint32_t)(now - client->last_done_ms) < window)))
        return false;
    memcpy(out, client->request, IGNITION_REQUEST_BYTES);
    *length = IGNITION_REQUEST_BYTES;
    return true;
}
void IgnitionClient_TxAccepted(IgnitionClient *client, const uint8_t *wire, size_t length,
                               uint32_t now)
{
    if (!IgnitionClient_RequestMatches(client, wire, length))
        return;
    if (!client->sent)
        client->first_tx_ms = now;
    client->attempts++;
    client->waiting_tx = client->sent = true;
}
void IgnitionClient_TxDone(IgnitionClient *client, const uint8_t *wire, size_t length, uint32_t now,
                           uint32_t jitter, bool timed_out)
{
    if (!IgnitionClient_RequestMatches(client, wire, length) || !client->waiting_tx)
        return;
    client->waiting_tx = false;
    client->last_done_ms = now;
    client->jitter_ms = jitter > IGNITION_MAX_JITTER_MS ? IGNITION_MAX_JITTER_MS : jitter;
    if (timed_out && client->request[IGNITION_ACTION_OFFSET] == IGNITION_FIRE)
    {
        client->pending = false;
        client->operation = IGNITION_OPERATION_UNKNOWN;
    }
}
void IgnitionClient_Receive(IgnitionClient *client, const uint8_t *wire, size_t length,
                            uint32_t now)
{
    IgnitionPacket reply;
    if (!client || !client->pending || !client->sent ||
        (uint32_t)(now - client->started_ms) > client->operation_ms ||
        Ignition_Decode(&client->auth, IGNITION_REPLY, wire, length, &reply) != IGNITION_CODEC_OK ||
        reply.epoch != client->epoch || reply.id != st_get32(client->request + IGNITION_ID_OFFSET))
        return;
    bool query = client->request[IGNITION_ACTION_OFFSET] == IGNITION_QUERY;
    uint8_t kind = reply.data[0];
    if ((!query && kind != IGNITION_COMMAND_REPLY) ||
        (query && kind != IGNITION_QUERY_REPLY && kind != IGNITION_DETAIL_REPLY) ||
        (kind == IGNITION_DETAIL_REPLY && !client->request[IGNITION_MODE_OFFSET]))
        return;
    unsigned index = kind == IGNITION_DETAIL_REPLY ? 1u : 0u;
    uint8_t bit = (uint8_t)(1u << index);
    if ((client->seen & bit) && memcmp(client->replies[index], reply.data, 10))
    {
        IgnitionClient_Fail(client);
        client->operation = IGNITION_OPERATION_UNKNOWN;
        return;
    }
    memcpy(client->replies[index], reply.data, 10);
    client->seen |= bit;
    uint8_t required = query && client->request[IGNITION_MODE_OFFSET] ? 3 : 1;
    if (client->seen != required)
        return;
    const uint8_t *main = client->replies[0];
    client->result = main[1];
    client->state = main[2];
    client->reason = main[3];
    client->arm_id = st_get32(main + 4);
    client->received_ms = now;
    client->have_reply = true;
    client->have_details = required == 3;
    if (client->have_details)
    {
        memcpy(client->details, client->replies[1] + 1, 9);
        client->profile_id = st_get16(client->details + 6);
    }
    client->have_grant = false;
    if (!query && client->request[IGNITION_ACTION_OFFSET] == IGNITION_ARM &&
        client->result == IGNITION_ARM_GRANTED && client->state == IGNITION_ARMED && client->arm_id)
    {
        uint16_t remaining = st_get16(main + 8);
        client->grant_deadline_ms = client->first_tx_ms + remaining;
        client->have_grant =
            remaining && remaining != UINT16_MAX && (int32_t)(now - client->grant_deadline_ms) < 0;
        client->mode = client->request[IGNITION_MODE_OFFSET];
        client->profile_id = st_get16(client->request + IGNITION_PROFILE_OFFSET);
    }
    client->pending = client->waiting_tx = false;
    client->request_result = IGNITION_REQUEST_OK;
    client->operation =
        !query && (client->result == IGNITION_REJECTED || client->result == IGNITION_FAILED ||
                   client->result >= IGNITION_OLD_ID)
            ? IGNITION_OPERATION_REJECTED
            : IGNITION_OPERATION_COMPLETE;
}
