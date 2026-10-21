#include "st_ignition.h"
#include "st_link2.h"
#include "st_console.h"
#include "st_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, groups;
#define C(value)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if (!(value))                                                                              \
        {                                                                                          \
            fprintf(stderr, "ignition line%d: %s\n", __LINE__, #value);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define G(name)                                                                                    \
    do                                                                                             \
    {                                                                                              \
        groups++;                                                                                  \
        puts("PASS " name);                                                                        \
    } while (0)
static const uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
static const uint8_t acq[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t ctrl[12] = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
static IgnitionAuth IgnitionTest_GetAuth(void)
{
    IgnitionAuth auth = {.key_bound = true};
    memcpy(auth.key, key, 16);
    memcpy(auth.acquisition_uid, acq, 12);
    memcpy(auth.controller_uid, ctrl, 12);
    return auth;
}
static size_t IgnitionTest_Encode(IgnitionPacket packet, uint8_t out[ST2_MAX])
{
    IgnitionAuth auth = IgnitionTest_GetAuth();
    size_t length = 0;
    C(Ignition_Encode(&auth, &packet, out, &length) == IGNITION_CODEC_OK);
    return length;
}
static IgnitionPacket IgnitionTest_Decode(const uint8_t *wire, size_t length)
{
    IgnitionAuth auth = IgnitionTest_GetAuth();
    IgnitionPacket packet;
    C(Ignition_Decode(&auth, (length == IGNITION_REQUEST_BYTES ? IGNITION_REQUEST : IGNITION_REPLY),
                      wire, length, &packet) == IGNITION_CODEC_OK);
    return packet;
}
static IgnitionPacket IgnitionTest_MakeRequest(uint32_t id, uint8_t action, uint8_t mode,
                                               uint32_t arm, uint16_t profile)
{
    IgnitionPacket packet = {.type = IGNITION_REQUEST, .epoch = 7, .id = id};
    packet.data[0] = action;
    packet.data[1] = mode;
    st_put32(packet.data + 2, arm);
    st_put16(packet.data + 6, profile);
    return packet;
}
static void IgnitionTest_CheckCmac(void)
{
    const uint8_t message[64] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e,
                                 0x11, 0x73, 0x93, 0x17, 0x2a, 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03,
                                 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51, 0x30,
                                 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19,
                                 0x1a, 0x0a, 0x52, 0xef, 0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b,
                                 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};
    const uint8_t expected[4][16] = {{0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28, 0x7f, 0xa3,
                                      0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46},
                                     {0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44, 0xf7, 0x9b,
                                      0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c},
                                     {0xdf, 0xa6, 0x67, 0x47, 0xde, 0x9a, 0xe6, 0x30, 0x30, 0xca,
                                      0x32, 0x61, 0x14, 0x97, 0xc8, 0x27},
                                     {0x51, 0xf0, 0xbe, 0xbf, 0x7e, 0x3b, 0x9d, 0x92, 0xfc, 0x49,
                                      0x74, 0x17, 0x79, 0x36, 0x3c, 0xfe}};
    const size_t sizes[4] = {0, 16, 40, 64};
    for (unsigned i = 0; i < 4; i++)
    {
        uint8_t actual[16];
        C(Ignition_ComputeCmac(key, message, sizes[i], actual) == IGNITION_CODEC_OK);
        C(!memcmp(actual, expected[i], 16));
    }
    uint8_t tag[16];
    C(Ignition_ComputeCmac(NULL, message, 16, tag) == IGNITION_CODEC_INVALID);
    C(Ignition_ComputeCmac(key, NULL, 1, tag) == IGNITION_CODEC_INVALID);
    G("ignition RFC4493 CMAC vectors: empty, full block, partial block and multiple blocks");
}
static void IgnitionTest_CheckCodec(void)
{
    IgnitionAuth auth = IgnitionTest_GetAuth();
    const IgnitionPacket packets[] = {
        IgnitionTest_MakeRequest(1, IGNITION_QUERY, 0, 0, 0),
        IgnitionTest_MakeRequest(2, IGNITION_QUERY, 1, 19, 0),
        IgnitionTest_MakeRequest(3, IGNITION_ARM, 2, 0, 1),
        IgnitionTest_MakeRequest(4, IGNITION_FIRE, 2, 101, 1),
        IgnitionTest_MakeRequest(5, IGNITION_DISARM, 0, 0, 0),
        {.type = IGNITION_REPLY,
         .epoch = 7,
         .id = 1,
         .data = {IGNITION_QUERY_REPLY, IGNITION_SNAPSHOT}},
        {.type = IGNITION_REPLY, .epoch = 7, .id = 2, .data = {IGNITION_DETAIL_REPLY}}};
    for (unsigned i = 0; i < sizeof packets / sizeof packets[0]; i++)
    {
        uint8_t wire[ST2_MAX], changed[ST2_MAX];
        size_t length = IgnitionTest_Encode(packets[i], wire);
        C(length ==
          (packets[i].type == IGNITION_REQUEST ? IGNITION_REQUEST_BYTES : IGNITION_REPLY_BYTES));
        IgnitionPacket decoded;
        C(Ignition_Decode(&auth,
                          (length == IGNITION_REQUEST_BYTES ? IGNITION_REQUEST : IGNITION_REPLY),
                          wire, length, &decoded) == IGNITION_CODEC_OK);
        C(decoded.epoch == 7 && decoded.id == packets[i].id);
        st2_packet transport;
        C(st2_decode(&transport, wire, length));
        C(st2_encode(&transport, changed) == length && !memcmp(wire, changed, length));
        for (size_t j = 0; j < length; j++)
        {
            memcpy(changed, wire, length);
            changed[j] ^= 1;
            C(Ignition_Decode(
                  &auth, (length == IGNITION_REQUEST_BYTES ? IGNITION_REQUEST : IGNITION_REPLY),
                  changed, length, &decoded) != IGNITION_CODEC_OK);
        }
        for (size_t j = 0; j < length; j++)
            C(Ignition_Decode(
                  &auth, (length == IGNITION_REQUEST_BYTES ? IGNITION_REQUEST : IGNITION_REPLY),
                  wire, j, &decoded) != IGNITION_CODEC_OK);
        memcpy(changed, wire, length);
        changed[13] ^= 1;
        st_put16(changed + length - 2, st_crc16(changed, length - 2));
        C(Ignition_Decode(&auth,
                          (length == IGNITION_REQUEST_BYTES ? IGNITION_REQUEST : IGNITION_REPLY),
                          changed, length, &decoded) == IGNITION_CODEC_AUTH);
        IgnitionAuth wrong = auth;
        wrong.controller_uid[0] ^= 1;
        C(Ignition_Decode(&wrong,
                          (length == IGNITION_REQUEST_BYTES ? IGNITION_REQUEST : IGNITION_REPLY),
                          wire, length, &decoded) == IGNITION_CODEC_AUTH);
        wrong = auth;
        wrong.key[0] ^= 1;
        C(Ignition_Decode(&wrong,
                          (length == IGNITION_REQUEST_BYTES ? IGNITION_REQUEST : IGNITION_REPLY),
                          wire, length, &decoded) == IGNITION_CODEC_AUTH);
    }
    uint8_t wire[ST2_MAX];
    size_t length = 0;
    IgnitionPacket invalid = IgnitionTest_MakeRequest(1, IGNITION_DISARM, 1, 0, 0);
    C(Ignition_Encode(&auth, &invalid, wire, &length) == IGNITION_CODEC_INVALID);
    invalid = IgnitionTest_MakeRequest(1, IGNITION_FIRE, 1, 0, 1);
    C(Ignition_Encode(&auth, &invalid, wire, &length) == IGNITION_CODEC_INVALID);
    invalid = IgnitionTest_MakeRequest(1, IGNITION_QUERY, 1, 65536, 0);
    C(Ignition_Encode(&auth, &invalid, wire, &length) == IGNITION_CODEC_OK);
    auth.key_bound = false;
    invalid = packets[0];
    C(Ignition_Encode(&auth, &invalid, wire, &length) == IGNITION_CODEC_KEY_UNBOUND);
    G("ignition IGNITION_REQUEST_BYTES/35B codec, full transport roundtrip, CRC/MAC/UID/key "
      "mutations and strict action fields");
}
static void IgnitionTest_CheckServer(void)
{
    IgnitionServer server;
    IgnitionServer_Init(&server, acq, ctrl);
    C(IgnitionServer_SetKey(&server, key) == IGNITION_KEY_OK);
    C(IgnitionServer_SetKey(&server, server.auth.key) == IGNITION_KEY_OK);
    C(!memcmp(server.auth.key, key, 16));
    C(IgnitionServer_SetSession(&server, 7, true) == IGNITION_SESSION_OK);
    C(IgnitionServer_SetKey(&server, key) == IGNITION_KEY_BUSY);
    uint8_t request[ST2_MAX], reply[ST2_MAX], saved[ST2_MAX], detail[ST2_MAX];
    size_t length =
        IgnitionTest_Encode(IgnitionTest_MakeRequest(1, IGNITION_ARM, 1, 0, 1), request);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
    IgnitionPacket decoded = IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES);
    C(decoded.data[1] == IGNITION_REJECTED && decoded.data[2] == IGNITION_DISABLED &&
      decoded.data[3] == IGNITION_REASON_UNBOUND && !st_get32(decoded.data + 4));
    memcpy(saved, reply, IGNITION_REPLY_BYTES);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES &&
      !memcmp(saved, reply, IGNITION_REPLY_BYTES));
    length = IgnitionTest_Encode(IgnitionTest_MakeRequest(1, IGNITION_QUERY, 1, 1, 0), request);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
    C(IgnitionServer_Poll(&server, detail) == IGNITION_REPLY_BYTES &&
      !IgnitionServer_Poll(&server, reply));
    decoded = IgnitionTest_Decode(detail, IGNITION_REPLY_BYTES);
    C(decoded.data[0] == IGNITION_DETAIL_REPLY);
    C(!(st_get16(decoded.data + 1) & 0x3Fu)); /* Physical inputs are UNKNOWN, never guessed. */
    C(st_get16(decoded.data + 5) == UINT16_MAX && !st_get16(decoded.data + 7) && !decoded.data[9]);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
    C(IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES).data[1] == IGNITION_REJECTED);
    C(IgnitionServer_Poll(&server, reply) == IGNITION_REPLY_BYTES &&
      !memcmp(detail, reply, IGNITION_REPLY_BYTES));
    length = IgnitionTest_Encode(IgnitionTest_MakeRequest(1, IGNITION_ARM, 2, 0, 1), request);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
    C(IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES).data[1] == IGNITION_CONFLICT);
    length = IgnitionTest_Encode(IgnitionTest_MakeRequest(3, IGNITION_DISARM, 0, 0, 0), request);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
    decoded = IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES);
    C(decoded.data[1] == IGNITION_FAILED && decoded.data[3] == IGNITION_REASON_UNBOUND);
    length = IgnitionTest_Encode(IgnitionTest_MakeRequest(2, IGNITION_FIRE, 1, 99, 1), request);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
    C(IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES).data[1] == IGNITION_OLD_ID);
    length = IgnitionTest_Encode(IgnitionTest_MakeRequest(4, IGNITION_FIRE, 1, 99, 1), request);
    C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
    C(IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES).data[1] == IGNITION_REJECTED);
    /* Queries never evict the independently retained command outcomes. */
    for (uint32_t id = 2; id < 20; id++)
    {
        length =
            IgnitionTest_Encode(IgnitionTest_MakeRequest(id, IGNITION_QUERY, 0, 1, 0), request);
        C(IgnitionServer_Receive(&server, request, length, reply) == IGNITION_REPLY_BYTES);
        C(IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES).data[1] == IGNITION_REJECTED);
    }
    C(IgnitionServer_SetSession(&server, 0, false) == IGNITION_SESSION_OK);
    C(!IgnitionServer_Receive(&server, request, length, reply));
    C(IgnitionServer_SetSession(&server, 7, true) == IGNITION_SESSION_RETIRED);
    C(IgnitionServer_SetSession(&server, 8, true) == IGNITION_SESSION_OK);
    C(!IgnitionServer_Receive(&server, request, length, reply));
    IgnitionServer_Init(&server, (uint8_t[12]){0}, ctrl);
    C(IgnitionServer_SetKey(&server, key) == IGNITION_KEY_INVALID);
    G("ignition UNBOUND rejection, UNKNOWN physical inputs, DISARM cannot confirm closure, cache "
      "isolation, high-water fence and retired epochs");
}
static void IgnitionTest_InitClient(IgnitionClient *client)
{
    IgnitionClient_Init(client, acq, ctrl, 500, 10000, 4);
    C(IgnitionClient_SetKey(client, key) == IGNITION_KEY_OK);
    C(IgnitionClient_SetSession(client, 7, true) == IGNITION_SESSION_OK);
}
static void IgnitionTest_CheckClient(void)
{
    IgnitionServer server;
    IgnitionServer_Init(&server, acq, ctrl);
    C(IgnitionServer_SetKey(&server, key) == IGNITION_KEY_OK);
    C(IgnitionServer_SetSession(&server, 7, true) == IGNITION_SESSION_OK);
    IgnitionClient client;
    IgnitionTest_InitClient(&client);
    uint8_t request[ST2_MAX], main[ST2_MAX], detail[ST2_MAX], retry[ST2_MAX];
    size_t length = 0, retry_length = 0;
    C(IgnitionClient_Query(&client, true, 0, 0) == IGNITION_REQUEST_OK);
    C(IgnitionClient_TxDue(&client, 0, request, &length) && length == IGNITION_REQUEST_BYTES);
    IgnitionClient_TxAccepted(&client, request, length, 0);
    C(!IgnitionClient_TxDue(&client, 999, retry, &retry_length));
    IgnitionClient_TxDone(&client, request, length, 20, 0, false);
    C(IgnitionServer_Receive(&server, request, length, main) == IGNITION_REPLY_BYTES);
    C(IgnitionServer_Poll(&server, detail) == IGNITION_REPLY_BYTES);
    IgnitionClient_Receive(&client, detail, IGNITION_REPLY_BYTES, 30);
    C(client.pending && !client.have_reply);
    C(IgnitionClient_TxDue(&client, 1020, retry, &retry_length));
    C(retry_length == length && !memcmp(retry, request, length));
    IgnitionClient_TxAccepted(&client, retry, length, 1020);
    IgnitionClient_TxDone(&client, retry, length, 1040, 0, false);
    IgnitionClient_Receive(&client, main, IGNITION_REPLY_BYTES, 1050);
    C(!client.pending && client.have_reply && client.have_details && client.profile_id == 0);
    C(client.state == IGNITION_DISABLED && client.reason == IGNITION_REASON_UNBOUND);
    C(IgnitionClient_RequestFire(&client, 1051) == IGNITION_REQUEST_NOT_ARMED);
    C(IgnitionClient_RequestArm(&client, 1, 0, 1051) == IGNITION_REQUEST_NO_PROFILE);
    /* Simulate a peer grant only to test transport: no output driver exists. */
    C(IgnitionClient_RequestArm(&client, 1, 1, 1060) == IGNITION_REQUEST_OK);
    C(IgnitionClient_TxDue(&client, 1060, request, &length));
    IgnitionClient_TxAccepted(&client, request, length, 1060);
    IgnitionClient_TxDone(&client, request, length, 1080, 0, false);
    IgnitionPacket grant = {
        .type = IGNITION_REPLY, .epoch = 7, .id = st_get32(request + IGNITION_ID_OFFSET)};
    grant.data[1] = IGNITION_ARM_GRANTED;
    grant.data[2] = IGNITION_ARMED;
    st_put32(grant.data + 4, 99);
    st_put16(grant.data + 8, 30000);
    C(IgnitionTest_Encode(grant, main) == IGNITION_REPLY_BYTES);
    IgnitionClient_Receive(&client, main, IGNITION_REPLY_BYTES, 1100);
    C(client.have_grant && client.grant_deadline_ms == 31060);
    C(IgnitionClient_RequestFire(&client, 1110) == IGNITION_REQUEST_OK);
    C(IgnitionClient_TxDue(&client, 1110, request, &length));
    IgnitionClient_TxAccepted(&client, request, length, 1110);
    IgnitionClient_TxDone(&client, request, length, 1130, 0, false);
    C(!IgnitionClient_TxDue(&client, 1629, retry, &retry_length));
    C(!IgnitionClient_TxDue(&client, 1630, retry, &retry_length));
    C(!client.pending && client.operation == IGNITION_OPERATION_UNKNOWN && client.attempts == 1);
    C(IgnitionClient_RequestFire(&client, 1631) == IGNITION_REQUEST_NOT_ARMED);
    C(IgnitionClient_Query(&client, false, st_get32(request + IGNITION_ID_OFFSET), 1632) ==
      IGNITION_REQUEST_OK);
    C(IgnitionClient_TxDue(&client, 1632, request, &length));
    IgnitionClient_TxAccepted(&client, request, length, 1632);
    C(IgnitionClient_RequestDisarm(&client, 1633) == IGNITION_REQUEST_OK);
    IgnitionClient_TxDone(&client, request, length, 1640, 0, false);
    C(!client.waiting_tx && client.attempts == 0); /* Old TX_DONE cannot complete new DISARM. */
    C(IgnitionClient_TxDue(&client, 1641, retry, &retry_length));
    C(retry[IGNITION_ACTION_OFFSET] == IGNITION_DISARM);
    C(IgnitionClient_SetSession(&client, 0, false) == IGNITION_SESSION_OK);
    C(IgnitionClient_SetSession(&client, 7, true) == IGNITION_SESSION_RETIRED);
    C(IgnitionClient_SetSession(&client, 8, true) == IGNITION_SESSION_OK);
    client.next_command = UINT32_MAX;
    C(IgnitionClient_Query(&client, false, 0, 2000) == IGNITION_REQUEST_OK && !client.next_command);
    IgnitionClient_Fail(&client);
    C(IgnitionClient_Query(&client, false, 0, 2001) == IGNITION_REQUEST_EXHAUSTED);
    G("ignition frozen full-query reorder/retry, conservative grant deadline, FIRE exactly one "
      "transmission, uncertain result and DISARM preemption");
}
typedef struct
{
    char output[256];
    unsigned calls;
    bool idle;
    uint8_t key[16];
} IgnitionConsoleFixture;
static void IgnitionTest_Emit(void *ctx, const char *text)
{
    IgnitionConsoleFixture *fixture = ctx;
    (void)snprintf(fixture->output, sizeof fixture->output, "%s", text);
}
static bool IgnitionTest_Idle(void *ctx)
{
    return ((IgnitionConsoleFixture *)ctx)->idle;
}
static IgnitionKeyResult IgnitionTest_SetKey(void *ctx, const uint8_t *value)
{
    IgnitionConsoleFixture *fixture = ctx;
    fixture->calls++;
    if (value)
        memcpy(fixture->key, value, 16);
    else
        memset(fixture->key, 0, 16);
    return IGNITION_KEY_OK;
}
static void IgnitionTest_Feed(st_console *console, const char *text)
{
    while (*text)
        st_console_feed(console, (uint8_t)*text++);
}
static void IgnitionTest_CheckConsoleUi(void)
{
    IgnitionConsoleFixture fixture = {.idle = true};
    st_pairing pairing = {0};
    st_console console = {.pair = &pairing,
                          .ctx = &fixture,
                          .idle = IgnitionTest_Idle,
                          .emit = IgnitionTest_Emit,
                          .ignition_key = IgnitionTest_SetKey};
    IgnitionTest_Feed(&console, "IGNKEY 2b7e151628aed2a6abf7158809cf4f3c\n");
    C(fixture.calls == 1 && !memcmp(fixture.key, key, 16));
    C(strstr(fixture.output, "RAM ONLY") && !strstr(fixture.output, "2b7e"));
    for (unsigned i = 0; i < sizeof console.line; i++)
        C(!console.line[i]);
    IgnitionTest_Feed(&console, "IGNKEY 00000000000000000000000000000000\n");
    IgnitionTest_Feed(&console, "IGNKEY 2b7e151628aed2a6abf7158809cf4f3z\n");
    C(fixture.calls == 1);
    fixture.idle = false;
    IgnitionTest_Feed(&console, "IGNKEY CLEAR\n");
    C(fixture.calls == 1);
    fixture.idle = true;
    IgnitionTest_Feed(&console, "IGNKEY CLEAR\n");
    C(fixture.calls == 2);
    G("ignition local key provisioning without echo / malformed and busy rejection");
}
typedef struct
{
    uint32_t now, ends;
    unsigned starts, stops;
    bool enabled, allow;
} IgnitionTest_Output;
static uint32_t IgnitionTest_Now(void *ctx)
{
    return ((IgnitionTest_Output *)ctx)->now;
}
static bool IgnitionTest_Allowed(void *ctx, uint8_t mode)
{
    return ((IgnitionTest_Output *)ctx)->allow && (mode == 1 || mode == 2);
}
static bool IgnitionTest_Running(void *ctx)
{
    IgnitionTest_Output *output = ctx;
    return output->enabled && (int32_t)(output->now - output->ends) < 0;
}
static IgnitionOutputResult IgnitionTest_Start(void *ctx)
{
    IgnitionTest_Output *output = ctx;
    if (IgnitionTest_Running(ctx))
        return IGNITION_OUTPUT_BUSY;
    output->starts++;
    output->enabled = true;
    output->ends = output->now + IGNITION_PULSE_MS;
    return IGNITION_OUTPUT_OK;
}
static IgnitionOutputResult IgnitionTest_Stop(void *ctx)
{
    IgnitionTest_Output *output = ctx;
    output->enabled = false;
    output->stops++;
    return IGNITION_OUTPUT_OK;
}
static IgnitionPacket IgnitionTest_Exchange(IgnitionServer *server, uint32_t id, uint8_t action,
                                            uint8_t mode, uint32_t arm, uint16_t profile)
{
    uint8_t request[ST2_MAX], reply[ST2_MAX];
    size_t length =
        IgnitionTest_Encode(IgnitionTest_MakeRequest(id, action, mode, arm, profile), request);
    C(IgnitionServer_Receive(server, request, length, reply) == IGNITION_REPLY_BYTES);
    return IgnitionTest_Decode(reply, IGNITION_REPLY_BYTES);
}
static void IgnitionTest_CheckBoundOutput(void)
{
    IgnitionServer server;
    IgnitionServer_Init(&server, acq, ctrl);
    IgnitionTest_Output output = {.allow = true, .now = UINT32_MAX - 9000u};
    C(IgnitionServer_BindOutput(
          &server,
          (IgnitionOutputPort){&output, IgnitionTest_Now, IgnitionTest_Allowed, IgnitionTest_Start,
                               IgnitionTest_Stop, IgnitionTest_Running}) == IGNITION_OUTPUT_OK);
    C(!IgnitionTest_Running(&output));
    C(IgnitionServer_SetKey(&server, key) == IGNITION_KEY_OK);
    C(IgnitionServer_SetSession(&server, 7, true) == IGNITION_SESSION_OK);
    C(IgnitionTest_Exchange(&server, 1, IGNITION_FIRE, 1, 1, 1).data[1] == IGNITION_REJECTED);
    C(!output.starts);
    IgnitionPacket reply = IgnitionTest_Exchange(&server, 2, IGNITION_ARM, 1, 0, 1);
    C(reply.data[1] == IGNITION_ARM_GRANTED && reply.data[2] == IGNITION_ARMED);
    uint32_t arm = st_get32(reply.data + 4), deadline = server.deadline_ms;
    C(arm && st_get16(reply.data + 8) == IGNITION_ARM_MS && !output.starts);
    output.now += 9999;
    C(IgnitionTest_Exchange(&server, 2, IGNITION_ARM, 1, 0, 1).data[1] == IGNITION_ARM_GRANTED);
    C(server.deadline_ms == deadline);
    output.now++;
    C(IgnitionTest_Exchange(&server, 3, IGNITION_FIRE, 1, arm, 1).data[1] == IGNITION_REJECTED);
    C(!output.starts && server.state == IGNITION_SAFE);
    reply = IgnitionTest_Exchange(&server, 4, IGNITION_ARM, 2, 0, 1);
    arm = st_get32(reply.data + 4);
    C(IgnitionTest_Exchange(&server, 5, IGNITION_FIRE, 1, arm, 1).data[1] == IGNITION_REJECTED);
    C(!output.starts);
    C(IgnitionTest_Exchange(&server, 6, IGNITION_FIRE, 2, arm, 1).data[1] ==
      IGNITION_FIRE_ACCEPTED);
    C(output.starts == 1 && IgnitionTest_Running(&output));
    C(IgnitionTest_Exchange(&server, 6, IGNITION_FIRE, 2, arm, 1).data[1] ==
      IGNITION_FIRE_ACCEPTED);
    C(output.starts == 1);
    output.now += IGNITION_PULSE_MS - 1u;
    C(IgnitionTest_Running(&output));
    output.now++;
    C(!IgnitionTest_Running(&output)); /* Peripheral ends even without server/foreground service. */
    IgnitionServer_Service(&server);
    C(server.state == IGNITION_LOCKOUT);
    C(IgnitionTest_Exchange(&server, 6, IGNITION_FIRE, 2, arm, 1).data[1] == IGNITION_OUTPUT_ENDED);
    C(IgnitionTest_Exchange(&server, 7, IGNITION_ARM, 1, 0, 1).data[1] == IGNITION_REJECTED);
    C(IgnitionTest_Exchange(&server, 8, IGNITION_DISARM, 0, 0, 0).data[1] == IGNITION_DISARMED);
    reply = IgnitionTest_Exchange(&server, 9, IGNITION_ARM, 1, 0, 1);
    arm = st_get32(reply.data + 4);
    C(IgnitionTest_Exchange(&server, 11, IGNITION_DISARM, 0, 0, 0).data[1] == IGNITION_DISARMED);
    C(IgnitionTest_Exchange(&server, 10, IGNITION_FIRE, 1, arm, 1).data[1] == IGNITION_OLD_ID);
    C(output.starts == 1);
    reply = IgnitionTest_Exchange(&server, 12, IGNITION_ARM, 1, 0, 1);
    C(IgnitionTest_Exchange(&server, 13, IGNITION_FIRE, 1, st_get32(reply.data + 4), 1).data[1] ==
      IGNITION_FIRE_ACCEPTED);
    C(IgnitionTest_Running(&output));
    output.allow = false;
    IgnitionServer_Service(&server);
    C(!IgnitionTest_Running(&output) && server.reason == IGNITION_REASON_LOCAL);
    output.allow = true;
    C(IgnitionTest_Exchange(&server, 14, IGNITION_DISARM, 0, 0, 0).data[1] == IGNITION_DISARMED);
    reply = IgnitionTest_Exchange(&server, 15, IGNITION_ARM, 1, 0, 1);
    C(IgnitionTest_Exchange(&server, 16, IGNITION_FIRE, 1, st_get32(reply.data + 4), 1).data[1] ==
      IGNITION_FIRE_ACCEPTED);
    C(IgnitionServer_SetSession(&server, 0, false) == IGNITION_SESSION_OK);
    C(!IgnitionTest_Running(&output) && server.state == IGNITION_SAFE);
    G("bound signal: 10s ARM expiry/wrap, mode binding, one configured simulation pulse, duplicate FIRE, lockout, "
      "DISARM fence, local fault and reconnect cutoff");
}
static void IgnitionTest_Transfer(IgnitionClient *client, IgnitionServer *server, uint32_t now)
{
    uint8_t request[ST2_MAX], reply[ST2_MAX];
    size_t length = 0;
    C(IgnitionClient_TxDue(client, now, request, &length));
    IgnitionClient_TxAccepted(client, request, length, now);
    IgnitionClient_TxDone(client, request, length, now + 1u, 0, false);
    C(IgnitionServer_Receive(server, request, length, reply) == IGNITION_REPLY_BYTES);
    IgnitionClient_Receive(client, reply, IGNITION_REPLY_BYTES, now + 2u);
    if (IgnitionServer_Poll(server, reply))
        IgnitionClient_Receive(client, reply, IGNITION_REPLY_BYTES, now + 3u);
    C(!client->pending);
}
static void IgnitionTest_CheckLiveUi(const char *images)
{
    IgnitionTest_Output output = {.allow = true};
    IgnitionServer server;
    IgnitionServer_Init(&server, acq, ctrl);
    C(IgnitionServer_BindOutput(
          &server,
          (IgnitionOutputPort){&output, IgnitionTest_Now, IgnitionTest_Allowed, IgnitionTest_Start,
                               IgnitionTest_Stop, IgnitionTest_Running}) == IGNITION_OUTPUT_OK);
    C(IgnitionServer_SetKey(&server, key) == IGNITION_KEY_OK);
    C(IgnitionServer_SetSession(&server, 7, true) == IGNITION_SESSION_OK);
    IgnitionClient ignition;
    IgnitionTest_InitClient(&ignition);
    ignition.profile_id = ST_OUTPUT_PROFILE_ID;
    st2_client client = {.ignition = &ignition,
                         .link = ST2_LINK_UP,
                         .epoch = 7,
                         .have_status = true,
                         .next_command = 1,
                         .timing = {2000, 150, 500, 120000, 4}};
    st_ui ui;
    st_ui_init(&ui);
    ui.selection = 2;
    (void)st_ui2_handle(&ui, ST_UI_PRESS, &client, 0);
    ui.selection = 1;
    (void)st_ui2_handle(&ui, ST_UI_PRESS, &client, 0);
    C(ui.page == ST_UI_CONFIRM && ui.selection == 0);
    (void)st_ui2_handle(&ui, ST_UI_NEXT, &client, 0);
    C(st_ui2_handle(&ui, ST_UI_PRESS, &client, 0));
    IgnitionTest_Transfer(&ignition, &server, 1);
    st_ui2_service(&ui, &client, 4);
    C(ui.page == ST_UI_CONFIRM && ui.selection == 0 && !output.starts && ignition.have_grant);
    (void)st_ui2_handle(&ui, ST_UI_NEXT, &client, 4);
    C(st_ui2_handle(&ui, ST_UI_PRESS, &client, 4));
    IgnitionTest_Transfer(&ignition, &server, 5);
    C(output.starts == 1 && !client.pending && client.output_started);
    C(Ui_GetItemCount(&ui) == 2);
    (void)st_ui2_handle(&ui, ST_UI_BACK, &client, 8);
    C(ui.page == ST_UI_HOME && output.starts == 1);
    FILE *file = fopen(images, "wb");
    C(file);
    for (unsigned state = 0; state <= ST_TASK_UNKNOWN; state++)
    {
        client.task = (StTask){ST_TASK_OUTPUT, (StTaskState)state, 1, 0};
        ui.page = ST_UI_IGNITION_ONLY;
        ui.selection = 0;
        ui.mode = ST_TASK_OUTPUT;
        st_ui_model model;
        uint8_t pixels[1024];
        st_ui2_render(&ui, &client, (st_voltage_view){0}, 100, &model);
        C(!model.rows[2][0] && !model.rows[3][0]);
        st_ui_raster(&model, pixels);
        C(fwrite(pixels, 1, 1024, file) == 1024);
    }
    C(!fclose(file));
    G("R8 output simulation: independent default-BACK ARM/FIRE / one execution / no STOP menu / "
      "navigation no replay / eight state previews");
}
unsigned st_test_ignition(unsigned *out, const char *images)
{
    IgnitionTest_CheckCmac();
    IgnitionTest_CheckCodec();
    IgnitionTest_CheckServer();
    IgnitionTest_CheckClient();
    IgnitionTest_CheckConsoleUi();
    IgnitionTest_CheckBoundOutput();
    IgnitionTest_CheckLiveUi(images);
    *out += groups;
    return checks;
}
