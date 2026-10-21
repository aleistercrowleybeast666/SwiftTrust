#ifndef ST_LINK2_H
#define ST_LINK2_H
#include "st_core.h"
#include "st_ignition.h"
/* Internal operations. Wire messages share one R8 header and seven logical kinds. */
enum
{
    ST2_STATUS = 0x01,
    ST2_START,
    ST2_STOP,
    ST2_ACK,
    ST2_DETAILS_QUERY,
    ST2_DETAILS_REPLY,
    ST2_HELLO,
    ST2_HELLO_REPLY,
    ST2_COMMIT,
    ST2_CAL_COMMAND,
    ST2_CAL_QUERY,
    ST2_CAL_REPLY,
    ST2_IGN_REQUEST,
    ST2_IGN_REPLY,
    ST2_DISCOVER,
    ST2_ADVERTISE,
    ST2_SENSOR_COMMAND,
    ST2_SENSOR_QUERY,
    ST2_SENSOR_REPLY,
    ST2_TASK_QUERY,
    ST2_TASK_REPLY,
    ST2_STATUS_ACK,
    ST2_MAX = ST_PROTOCOL_MAX_BYTES
};
enum
{
    ST_WIRE_HELLO = 1,
    ST_WIRE_HELLO_ACK,
    ST_WIRE_STATUS,
    ST_WIRE_COMMAND,
    ST_WIRE_RESULT,
    ST_WIRE_QUERY,
    ST_WIRE_DATA
};
enum
{
    ST2_ACCEPTED,
    ST2_STATE_REJECT,
    ST2_EPOCH_REJECT,
    ST2_OLD,
    ST2_CONFLICT,
    ST2_COMPLETED,
    ST2_FAILED
};
enum
{
    ST2_TF_UNKNOWN,
    ST2_TF_READY,
    ST2_TF_OPEN,
    ST2_TF_FAULT
};
typedef struct
{
    uint8_t type, flags, section, fragment, count, data[16], uid[12], peer[12], tag[16];
    uint32_t epoch, id, pair_id;
    uint16_t mv;
    uint8_t mode, task_state, errors;
    uint64_t counter;
} st2_packet;
size_t st2_encode(const st2_packet *, uint8_t out[ST2_MAX]);
bool st2_decode(st2_packet *, const uint8_t *, size_t);
/* Durable callbacks MUST commit before returning true. They must be bounded
 * (or called from the radio task). No RAM-only fallback in operational firmware.
 * reserve_epoch commits the paired controller counter and a never-reused epoch.
 * reserve_counter atomically increments the controller's durable counter.
 * Fail on exhaustion/write failure. Identity/pairing is provisioned separately.
 * CRC and public UIDs are NOT authentication against a hostile transmitter. */
typedef struct
{
    void *ctx;
    bool (*reserve_epoch)(void *, uint64_t, uint32_t *);
    bool (*reserve_counter)(void *, uint64_t *);
} st2_nv_port;
typedef struct
{
    uint32_t heartbeat_ms, max_airtime_ms, ack_rto_ms, operation_ms;
    unsigned attempts;
} st2_timing;
bool st2_timing_valid(st2_timing);
typedef struct
{
    uint8_t tf;
    bool voltage_fresh, adc_fresh, quality_problem;
    uint16_t mv;
} st2_measurement;
/* Calibration callbacks run in one serialized foreground volume owner, never
 * RF/ADC ISR. request may do bounded synchronous FatFs IO while capture is IDLE.
 * locked excludes START through the whole wizard; reset cancels session drafts. */
typedef struct
{
    void *ctx;
    bool (*request)(void *, const uint8_t request[14]);
    bool (*snapshot)(void *, uint8_t view[64]);
    bool (*locked)(void *);
    bool (*prepare_start)(void *);
    void (*reset)(void *);
    bool (*start_waiting)(void *);
    bool (*start_failed)(void *);
    bool (*sensor_snapshot)(void *, uint8_t out[12]);
    void (*begin_start)(void *);
} st2_cal_port;
typedef struct
{
    uint32_t id;
    uint8_t request[ST2_MAX], reply[ST2_MAX], view[64], request_n, reply_n;
} StTransactionCache;
typedef struct
{
    st_logger *logger;
    IgnitionServer *ignition;
    st2_cal_port calibration;
    uint8_t cal_request[14], cal_view[64], cal_next;
    uint32_t cal_last_ms;
    bool cal_cached, cal_owned;
    st2_nv_port nv;
    st2_timing timing;
    uint8_t uid[12], peer[12], last_type, last_result, last_flags;
    uint32_t epoch, last_status_ms, pair_id;
    uint64_t last_counter;
    uint64_t now_us;
    uint32_t last_command, status_sequence;
    StTask task;
    bool start_deferred, output_started;
    uint32_t highest_id, cal_id;
    uint32_t peer_received_ms, peer_status_sequence;
    bool have_peer_status;
    StTransactionCache history[8];
    uint8_t history_next;
    bool offered, active, have_command, terminal_pending, have_status, quality_sticky;
    uint8_t previous_flags;
    uint16_t previous_mv;
    uint32_t detail_id;
    uint8_t detail_section, detail_next, snapshot[23];
    bool detail_cached;
} st2_server;
void st2_server_init(st2_server *, st_logger *, st2_nv_port, st2_timing, const uint8_t uid[12],
                     const uint8_t paired_controller[12], uint64_t durable_last_counter);
void st2_server_attach_calibration(st2_server *, st2_cal_port);
size_t st2_server_receive(st2_server *, const uint8_t *, size_t, st2_measurement, uint64_t now_us,
                          uint8_t out[ST2_MAX]);
/* Call only when the RF output slot is available. Terminal ACK precedes status.
 * Retain/retry the produced packet if transport acceptance subsequently fails. */
size_t st2_server_poll(st2_server *, st2_measurement, uint64_t, uint8_t out[ST2_MAX]);
/* Section 1 = immutable 23-byte acquisition diagnostic snapshot, two fragments.
 * rate/last sample/durable/drop/errors: five LE uint32, ADC age LE uint16,
 * byte22: have_sample bit0, have_durable bit1, clock_verified bit2, simulated bit3.
 * An unavailable sequence is identified by its flag, never by a magic value. */
typedef struct
{
    uint32_t epoch;
    uint32_t id;
    uint8_t section, seen, data[23];
    bool complete;
} st2_details;
size_t st2_details_begin(st2_details *, uint32_t epoch, uint32_t request_id, uint8_t out[ST2_MAX]);
bool st2_details_receive(st2_details *, const uint8_t *, size_t);
typedef enum
{
    ST2_LINK_DOWN,
    ST2_LINK_HELLO,
    ST2_LINK_COMMIT,
    ST2_LINK_UP
} st2_link_state;
typedef enum
{
    ST2_STOP_NONE,
    ST2_STOP_QUEUED,
    ST2_STOP_ACTIVE,
    ST2_STOP_UNCONFIRMED
} st2_stop_state;
typedef struct
{
    st2_nv_port nv;
    st2_timing timing;
    uint8_t uid[12], peer[12];
    st2_link_state link;
    st2_packet request;
    uint8_t cal_view[64], cal_seen;
    bool cal_have_view;
    uint32_t cal_received_ms;
    uint64_t counter;
    uint32_t epoch, received_ms, last_send_ms, started_ms, retry_jitter_ms;
    uint32_t status_sequence, next_command, pair_id;
    uint16_t mv;
    StTask task;
    uint8_t remote_task, remote_mode, errors;
    bool resumed, need_recovery;
    bool driver_fault;
    bool output_started;
    uint8_t sensor_view[12];
    bool sensor_have_view;
    st2_details details;
    uint32_t details_received_ms;
    uint8_t flags;
    unsigned attempts;
    bool have_status, pending, waiting_tx_done, have_sent;
    st_transaction_state transaction;
    st2_stop_state stop;
    uint32_t stop_requested_ms;
    IgnitionClient *ignition;
} st2_client;
typedef enum
{
    ST_LINK_REQUEST_REJECTED,
    ST_LINK_REQUEST_OK
} StLinkRequestResult;
StLinkRequestResult Link_RequestSensor(st2_client *, uint8_t action, bool enabled, uint32_t now_ms);
StLinkRequestResult Link_QueryTask(st2_client *, uint32_t now_ms);
StLinkRequestResult Link_QueryDetails(st2_client *, uint32_t now_ms);
typedef enum
{
    ST_LINK_WAIT,
    ST_LINK_OK,
    ST_LINK_SLOW,
    ST_LINK_LOST,
    ST_LINK_DRIVER_FAULT
} StLinkHealth;
StLinkHealth Link_GetHealth(bool have_status, uint32_t received_ms, uint32_t now_ms,
                            bool driver_fault);
bool Link_GetLed(StLinkHealth, uint32_t now_ms);
void st2_client_init(st2_client *, st2_nv_port, st2_timing, const uint8_t uid[12],
                     const uint8_t peer[12]);
bool st2_client_connect(st2_client *, uint32_t now_ms);
bool st2_client_receive(st2_client *, const uint8_t *, size_t, uint32_t now_ms);
bool st2_client_fresh(const st2_client *, uint32_t now_ms);
/* STOP during pending START queues intent without changing the in-flight ID.
 * tx_due orders a higher-ID STOP after TX completion and fresh telemetry.
 * Repeated STOP is idempotent; an unconfirmed STOP needs explicit retry/connect.
 * Neither status nor ACCEPTED confirms file closure. */
bool st2_client_command(st2_client *, uint8_t type, uint32_t now_ms);
/* NULL payload queries; commands carry action/unit/u64 milli-units/u32 gravity. */
bool st2_client_cal_command(st2_client *, const uint8_t payload[14], uint32_t now_ms);
bool st2_client_tx_due(st2_client *, uint32_t now_ms, uint8_t out[ST2_MAX], size_t *);
/* Invoke only after RF accepts the packet; TX_DONE anchors the ACK timer.
 * Notify completion even on a TX timeout, so bounded retry can proceed. */
void st2_client_tx_accepted(st2_client *);
void st2_client_tx_done(st2_client *, uint32_t now_ms, uint32_t random_jitter_ms);
#endif
