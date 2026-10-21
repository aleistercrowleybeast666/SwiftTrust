#ifndef __ST_IGNITION_H
#define __ST_IGNITION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../Common/Inc/st_project_config.h"

enum
{
    IGNITION_REQUEST = 0x0D,
    IGNITION_REPLY = 0x0E,
    IGNITION_MAX_BYTES = ST_PROTOCOL_MAX_BYTES,
    IGNITION_REQUEST_BYTES = 42,
    IGNITION_REPLY_BYTES = 44,
    IGNITION_ACTION_OFFSET = 16,
    IGNITION_MODE_OFFSET = 17,
    IGNITION_ID_OFFSET = 12,
    IGNITION_PROFILE_OFFSET = 22,
    IGNITION_KEY_BYTES = 16,
    IGNITION_DETAIL_BYTES = 9,
    IGNITION_QUERY = 0,
    IGNITION_ARM = 1,
    IGNITION_FIRE = 2,
    IGNITION_DISARM = 3,
    IGNITION_COMMAND_REPLY = 0,
    IGNITION_QUERY_REPLY = 1,
    IGNITION_DETAIL_REPLY = 2,
    IGNITION_DISABLED = 0,
    IGNITION_SAFE = 1,
    IGNITION_ARMED = 2,
    IGNITION_FIRING = 3,
    IGNITION_LOCKOUT = 4,
    IGNITION_FAULT = 5,
    IGNITION_BOOT_LOCKOUT = 6,
    IGNITION_REJECTED = 0,
    IGNITION_ARM_GRANTED = 1,
    IGNITION_FIRE_ACCEPTED = 2,
    IGNITION_OUTPUT_ENDED = 3,
    IGNITION_DISARMED = 4,
    IGNITION_FAILED = 5,
    IGNITION_NOT_SEEN = 6,
    IGNITION_OLD_ID = 7,
    IGNITION_CONFLICT = 8,
    IGNITION_EXPIRED = 9,
    IGNITION_RESULT_UNKNOWN = 10,
    IGNITION_SNAPSHOT = 11,
    IGNITION_REASON_NONE = 0,
    IGNITION_REASON_UNBOUND = 1,
    IGNITION_REASON_STATE = 2,
    IGNITION_REASON_LEASE = 3,
    IGNITION_REASON_PROFILE = 4,
    IGNITION_REASON_LOCAL = 5,
    IGNITION_REASON_OUTPUT = 6,
    IGNITION_ARM_MS = ST_OUTPUT_ARM_MS,
    IGNITION_PULSE_MS = ST_OUTPUT_HARDWARE_ENABLED ? ST_OUTPUT_REAL_DURATION_MS
                                                   : ST_OUTPUT_SIMULATION_MS,
    IGNITION_SIGNAL_PROFILE = ST_OUTPUT_PROFILE_ID,
    IGNITION_REASON_HISTORY = 16
};

typedef enum
{
    IGNITION_CODEC_OK,
    IGNITION_CODEC_INVALID,
    IGNITION_CODEC_KEY_UNBOUND,
    IGNITION_CODEC_FORMAT,
    IGNITION_CODEC_AUTH,
    IGNITION_CODEC_CONTEXT
} IgnitionCodecResult;
typedef enum
{
    IGNITION_KEY_OK,
    IGNITION_KEY_INVALID,
    IGNITION_KEY_BUSY
} IgnitionKeyResult;
typedef enum
{
    IGNITION_SESSION_OK,
    IGNITION_SESSION_INVALID,
    IGNITION_SESSION_RETIRED
} IgnitionSessionResult;
typedef enum
{
    IGNITION_REQUEST_OK,
    IGNITION_REQUEST_INVALID,
    IGNITION_REQUEST_KEY_UNBOUND,
    IGNITION_REQUEST_LINK_DOWN,
    IGNITION_REQUEST_BUSY,
    IGNITION_REQUEST_EXHAUSTED,
    IGNITION_REQUEST_NOT_ARMED,
    IGNITION_REQUEST_NO_PROFILE
} IgnitionRequestResult;
typedef enum
{
    IGNITION_OPERATION_IDLE,
    IGNITION_OPERATION_WAITING,
    IGNITION_OPERATION_COMPLETE,
    IGNITION_OPERATION_REJECTED,
    IGNITION_OPERATION_UNKNOWN
} IgnitionOperation;

typedef struct
{
    uint8_t key[16], acquisition_uid[12], controller_uid[12];
    bool key_bound;
} IgnitionAuth;
typedef struct
{
    uint32_t epoch;
    uint32_t id;
    uint8_t type, data[10];
} IgnitionPacket;

void Ignition_ClearMemory(void *, size_t);
IgnitionCodecResult Ignition_ComputeCmac(const uint8_t key[16], const uint8_t *, size_t,
                                         uint8_t tag[16]);
IgnitionCodecResult Ignition_Encode(const IgnitionAuth *, const IgnitionPacket *,
                                    uint8_t out[IGNITION_MAX_BYTES], size_t *);
IgnitionCodecResult Ignition_Decode(const IgnitionAuth *, uint8_t expected_type, const uint8_t *,
                                    size_t, IgnitionPacket *);

typedef struct
{
    uint32_t id;
    uint8_t request[8], reply[IGNITION_MAX_BYTES];
    bool valid;
} IgnitionCommandCache;
typedef enum
{
    IGNITION_OUTPUT_OK,
    IGNITION_OUTPUT_INVALID,
    IGNITION_OUTPUT_BUSY,
    IGNITION_OUTPUT_FAULT
} IgnitionOutputResult;
/* The board owns an autonomous single pulse and a synchronous forced-low stop.
 * This reports the MCU signal, never igniter continuity or successful ignition. */
typedef struct
{
    void *ctx;
    uint32_t (*now_ms)(void *);
    bool (*allowed)(void *, uint8_t mode);
    IgnitionOutputResult (*start)(void *);
    IgnitionOutputResult (*stop)(void *);
    bool (*running)(void *);
} IgnitionOutputPort;
typedef struct IgnitionServer
{
    IgnitionAuth auth;
    uint32_t epoch, retired_epoch;
    uint32_t last_command, last_query;
    IgnitionCommandCache commands[3];
    uint8_t query_request[8], query_reply[2][IGNITION_MAX_BYTES], query_count, query_next;
    bool query_cached;
    IgnitionOutputPort output;
    uint32_t arm_id, deadline_ms;
    uint8_t state, reason, mode;
    bool output_bound;
} IgnitionServer;
void IgnitionServer_Init(IgnitionServer *, const uint8_t acquisition_uid[12],
                         const uint8_t controller_uid[12]);
IgnitionKeyResult IgnitionServer_SetKey(IgnitionServer *, const uint8_t key_or_null[16]);
IgnitionSessionResult IgnitionServer_SetSession(IgnitionServer *, uint32_t epoch, bool active);
size_t IgnitionServer_Receive(IgnitionServer *, const uint8_t *, size_t,
                              uint8_t out[IGNITION_MAX_BYTES]);
size_t IgnitionServer_Poll(IgnitionServer *, uint8_t out[IGNITION_MAX_BYTES]);
IgnitionOutputResult IgnitionServer_BindOutput(IgnitionServer *, IgnitionOutputPort);
void IgnitionServer_Service(IgnitionServer *);
bool IgnitionServer_IsBusy(const IgnitionServer *);

typedef struct IgnitionClient
{
    IgnitionAuth auth;
    uint32_t epoch, retired_epoch, started_ms, first_tx_ms, last_done_ms, received_ms;
    uint32_t ack_rto_ms, operation_ms, jitter_ms, grant_deadline_ms;
    uint32_t next_command;
    uint32_t *transaction_source;
    uint16_t profile_id;
    unsigned attempts, max_attempts;
    uint8_t request[IGNITION_REQUEST_BYTES], replies[2][10], seen, mode;
    uint8_t state, result, reason, details[9];
    uint32_t arm_id;
    bool pending, waiting_tx, sent, have_reply, have_details, have_grant;
    IgnitionOperation operation;
    IgnitionRequestResult request_result;
} IgnitionClient;
void IgnitionClient_Init(IgnitionClient *, const uint8_t acquisition_uid[12],
                         const uint8_t controller_uid[12], uint32_t ack_rto_ms,
                         uint32_t operation_ms, unsigned attempts);
IgnitionKeyResult IgnitionClient_SetKey(IgnitionClient *, const uint8_t key_or_null[16]);
IgnitionSessionResult IgnitionClient_SetSession(IgnitionClient *, uint32_t epoch, bool active);
IgnitionRequestResult IgnitionClient_Query(IgnitionClient *, bool details,
                                           uint32_t target_command_id, uint32_t now_ms);
IgnitionRequestResult IgnitionClient_RequestArm(IgnitionClient *, uint8_t mode, uint16_t profile_id,
                                                uint32_t now_ms);
IgnitionRequestResult IgnitionClient_RequestFire(IgnitionClient *, uint32_t now_ms);
IgnitionRequestResult IgnitionClient_RequestDisarm(IgnitionClient *, uint32_t now_ms);
bool IgnitionClient_TxDue(IgnitionClient *, uint32_t now_ms, uint8_t out[IGNITION_MAX_BYTES],
                          size_t *);
bool IgnitionClient_RequestMatches(const IgnitionClient *, const uint8_t *, size_t);
void IgnitionClient_TxAccepted(IgnitionClient *, const uint8_t *, size_t, uint32_t now_ms);
void IgnitionClient_TxDone(IgnitionClient *, const uint8_t *, size_t, uint32_t now_ms,
                           uint32_t jitter_ms, bool timed_out);
void IgnitionClient_Receive(IgnitionClient *, const uint8_t *, size_t, uint32_t now_ms);
void IgnitionClient_Fail(IgnitionClient *);
#endif
