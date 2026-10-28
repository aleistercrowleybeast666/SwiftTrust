#ifndef ST_CORE_H
#define ST_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "../../Common/Inc/st_project_config.h"
#include "../../Common/Inc/st_task.h"

uint16_t st_crc16(const uint8_t *p, size_t n);
uint32_t st_crc32(const uint8_t *p, size_t n);
uint16_t st_get16(const uint8_t *p);
uint32_t st_get32(const uint8_t *p);
uint64_t st_get64(const uint8_t *p);
void st_put16(uint8_t *p, uint16_t v);
void st_put32(uint8_t *p, uint32_t v);
void st_put64(uint8_t *p, uint64_t v);
bool st_seq_newer(uint32_t a, uint32_t b);

enum
{
    ST_REC_CRC = 1,
    ST_REC_FORMAT = 2,
    ST_REC_ADC_FAULT = 4,
    ST_REC_SATURATED = 8,
    ST_REC_GAP = 16,
    ST_REC_TIME = 32,
    ST_REC_END = 0x8000
};
typedef struct
{
    uint64_t time_us;
    uint32_t sequence;
    /* ADS1220 signed24, MSB first in bytes[0..2]; remaining bytes are zero.
     * Padding preserves the existing SRAM queue budget, not a second channel. */
    uint8_t bytes[12];
    uint32_t reserved[2];
} st_capture;
_Static_assert(sizeof(st_capture) == 32, "capture SRAM budget");
typedef struct
{
    int32_t ch0, ch1;
    uint16_t status, flags;
} st_adc_sample;
st_adc_sample st_adc_decode(const uint8_t frame[12]);
void st_record(uint8_t out[32], const st_capture *capture, uint16_t extra_flags, uint32_t missing);

#define ST_RING_CAPACITY 1024u
typedef struct
{
    st_capture entries[ST_RING_CAPACITY];
    _Atomic(uint32_t) head, tail, dropped;
} st_ring;
/* One DMA-completion producer, one foreground consumer. Never reset while active. */
void st_ring_init(st_ring *q);
bool st_ring_push(st_ring *q, const st_capture *v);
bool st_ring_pop(st_ring *q, st_capture *v);
uint32_t st_ring_count(const st_ring *q);

typedef struct
{
    uint32_t mv;
    uint64_t time_us;
    bool present, valid;
} st_voltage;
typedef struct
{
    uint32_t threshold_mv[4], hysteresis_mv, freshness_ms;
    bool bound;
} st_voltage_policy;
typedef enum
{
    ST_V_UNKNOWN,
    ST_V_STALE,
    ST_V_LOW,
    ST_V_MEASURED
} st_voltage_kind;
typedef struct
{
    st_voltage_kind kind;
    uint8_t tier, led_mask;
    uint32_t mv, age_ms;
} st_voltage_view;
/* Divider ratio is numerator/denominator; absent/unverified circuit stays invalid. */
st_voltage st_voltage_from_adc(uint16_t code, uint32_t reference_mv, uint32_t numerator,
                               uint32_t denominator, uint64_t time_us, bool circuit_present,
                               bool scale_verified);
bool st_voltage_policy_valid(const st_voltage_policy *p);
st_voltage_view st_voltage_evaluate(const st_voltage_policy *p, const st_voltage *v,
                                    uint64_t now_us, st_voltage_view previous);

typedef enum
{
    ST_IO_DONE,
    ST_IO_WAIT,
    ST_IO_ERROR
} st_async_result;
typedef enum
{
    ST_FILE_OPEN_NEW,
    ST_FILE_WRITE,
    ST_FILE_SYNC,
    ST_FILE_CLOSE
} st_file_op;
typedef struct
{
    void *ctx;
    /* OPEN_NEW must exclusively create a unique file, never truncate an existing file.
     * DONE means accepted, not completed. Keep buffer until poll returns DONE.
     * WAIT means not submitted; ERROR latches fault. Both callbacks must be bounded. */
    st_async_result (*submit)(void *, st_file_op, const uint8_t *, size_t);
    st_async_result (*poll)(void *, size_t *completed_bytes);
} st_file_port;
typedef struct
{
    uint32_t adc_clock_hz, osr, rate_millihz, boot_epoch, profile_id, profile_version;
    int32_t zero_raw;
    int64_t nano_newtons_per_count, nano_volts_per_count_ch1;
    uint8_t gain_ch0, gain_ch1, wire_count;
    bool clock_verified, calibration_valid, simulated;
    int32_t calibration_zero_raw, session_zero_raw;
    uint32_t zero_samples;
    uint64_t zero_time_us;
    bool session_zero_valid;
    uint16_t adc_model;
    uint8_t channel_count, input_mux, operating_mode;
    bool pga_bypass;
    uint32_t reference_uv, excitation_uv, nominal_rate_millihz, conversion_cycles;
    uint32_t measured_rate_millihz;
} st_log_config;
/* Metadata for the current single AIN1-AIN2 board contract. Measured rate is
 * zero until DRDY timestamps provide a real observation. */
void Log_ApplyAds1220Metadata(st_log_config *, bool pga_bypass, uint32_t measured_rate_millihz);
typedef enum
{
    ST_IDLE,
    ST_STARTING,
    ST_CAPTURING,
    ST_STOPPING,
    ST_FAULT
} st_capture_state;
#define ST_STAGE_BYTES 4096u
typedef struct
{
    st_ring ring;
    uint8_t header[512], stage[2][ST_STAGE_BYTES];
    size_t used[2];
    uint32_t stage_sequence[2];
    uint64_t stage_order[2], next_order;
    bool stage_has_sample[2];
    st_log_config config;
    st_file_port port;
    st_capture_state state;
    _Atomic(bool) capture_enabled;
    _Atomic(uint32_t) source_errors;
    unsigned phase, fill;
    uint32_t generation;
    bool inflight, have_sample, have_written, have_durable, footer_queued;
    st_file_op operation;
    int active_stage;
    size_t submitted_bytes;
    uint32_t last_sequence, written_sequence, durable_sequence, sync_sequence, adc_errors;
    uint64_t last_time_us, last_flush_us, last_sync_us;
} st_logger;
void st_logger_init(st_logger *l, st_log_config cfg, st_file_port port);
bool st_logger_start(st_logger *l, uint64_t now_us);
bool st_logger_stop(st_logger *l);
void st_logger_source_error_isr(st_logger *l);
uint32_t st_logger_error_count(const st_logger *l);
bool st_logger_offer_isr(st_logger *l, const st_capture *capture);
/* At most 64 decodes and one submit per call. Call regularly in storage task. */
void st_logger_service(st_logger *l, uint64_t now_us);

enum
{
    ST_MSG_START = 1,
    ST_MSG_STOP = 2,
    ST_MSG_QUERY = 3,
    ST_MSG_HEARTBEAT = 4,
    ST_MSG_ACK = 0x80,
    ST_MSG_STATUS = 0x81,
    ST_PACKET_MAX = 49
};
enum
{
    ST_ACK_ACCEPTED,
    ST_ACK_STATE,
    ST_ACK_BOOT,
    ST_ACK_OLD,
    ST_ACK_CONFLICT
};
typedef struct
{
    uint8_t type, length, payload[34];
    uint32_t session, sequence;
} st_packet;
size_t st_packet_encode(const st_packet *p, uint8_t out[ST_PACKET_MAX]);
bool st_packet_decode(st_packet *p, const uint8_t *in, size_t length);
typedef struct
{
    uint32_t boot_epoch, last_command, rate_millihz, last_sample, durable_sample;
    uint16_t mv, voltage_age_ms, dropped, adc_errors, adc_age_ms;
    uint8_t state, storage, voltage_flags, quality_flags;
} st_status;
void st_status_pack(uint8_t out[34], const st_status *s);
bool st_status_unpack(st_status *s, const uint8_t in[34]);
typedef struct
{
    st_logger *logger;
    uint32_t session, boot_epoch, last_command, status_sequence;
    bool have_command;
    uint8_t last_type, last_result, last_state;
} st_server;
/* Epoch must monotonically increase without wrap within the pairing lifetime.
 * Pairing/session provisioning and RF authentication are outside this codec. */
void st_server_init(st_server *s, st_logger *l, uint32_t session, uint32_t epoch);
size_t st_server_handle(st_server *s, const uint8_t *in, size_t n, st_status measured_status,
                        uint64_t now_us, uint8_t out[ST_PACKET_MAX]);

typedef enum
{
    ST_TX_NONE,
    ST_TX_WAIT_ACK,
    ST_TX_WAIT_STATE,
    ST_TX_COMPLETE,
    ST_TX_REJECTED,
    ST_TX_TIMEOUT,
    ST_TX_REBOOT
} st_transaction_state;
typedef struct
{
    uint32_t session, boot_epoch, last_status_sequence, next_command;
    uint32_t received_ms, deadline_ms, retry_ms, last_send_ms;
    unsigned attempts, max_attempts;
    bool known, pending;
    st_status status;
    st_packet request;
    st_transaction_state transaction;
} st_client;
void st_client_init(st_client *c, uint32_t session, uint32_t retry_ms, unsigned attempts);
bool st_client_fresh(const st_client *c, uint32_t now_ms);
bool st_client_command(st_client *c, uint8_t type, uint32_t now_ms);
bool st_client_receive(st_client *c, const uint8_t *in, size_t n, uint32_t now_ms);
bool st_client_tx_due(st_client *c, uint32_t now_ms, uint8_t out[ST_PACKET_MAX], size_t *n);
void st_client_tx_sent(st_client *c, uint32_t now_ms);
uint32_t st_client_voltage_age(const st_client *c, uint32_t now_ms);
#endif
