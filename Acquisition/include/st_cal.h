#ifndef ST_CAL_H
#define ST_CAL_H
#include "st_core.h"
enum
{
    ST_CAL_PROFILE_BYTES = 256,
    ST_CAL_G0_UM_S2 = 9806650,
    ST_CAL_MIN_SAMPLES = 16,
    ST_CAL_MAX_SAMPLES = 8192
};
typedef enum
{
    ST_CAL_GRAMS,
    ST_CAL_KILOGRAMS,
    ST_CAL_NEWTONS
} st_cal_unit;
/* Always milli-units: 1000 means1g,1kg or1N according to unit, never an implicit kg.
 * Mass uses stored gravity in micrometres/s^2. Direct force uses gravity=0. */
typedef struct
{
    st_cal_unit unit;
    uint64_t milli_units;
    uint32_t gravity_um_s2;
} st_cal_reference;
typedef struct
{
    uint32_t profile_id, sensor_id, config_id, adc_clock_hz, osr, rate_millihz;
    uint32_t excitation_nominal_uv, filter_id;
    uint64_t rated_force_nn;
    uint8_t gain0, gain1, wire_count, power_mode;
    bool verified, simulated;
} st_cal_binding;
typedef struct
{
    uint32_t samples, max_span_counts, min_delta_counts, settle_us, timeout_us, zero_max_age_us;
} st_cal_policy;
typedef struct
{
    uint32_t count;
    int64_t sum;
    int32_t minimum, maximum, mean;
    uint64_t last_us;
    uint32_t last_sequence;
} st_cal_window;
typedef struct
{
    st_cal_binding binding;
    st_cal_policy policy;
    st_cal_reference reference;
    uint32_t revision;
    int32_t zero_raw, loaded_raw;
    int64_t nano_newtons_per_count, reference_force_nn;
    st_cal_window zero, loaded;
    uint64_t began_us, completed_us;
    bool valid;
} st_cal_profile;
typedef enum
{
    ST_CAL_INACTIVE,
    ST_CAL_ZERO_READY,
    ST_CAL_ZERO_COLLECT,
    ST_CAL_LOAD_READY,
    ST_CAL_LOAD_COLLECT,
    ST_CAL_REVIEW,
    ST_CAL_COMMITTED,
    ST_CAL_CANCELLED,
    ST_CAL_FAILED
} st_cal_state;
typedef enum
{
    ST_CAL_OK,
    ST_CAL_CONFIG,
    ST_CAL_BUSY,
    ST_CAL_REFERENCE,
    ST_CAL_ADC,
    ST_CAL_SEQUENCE,
    ST_CAL_CLOCK,
    ST_CAL_TIMEOUT,
    ST_CAL_UNSTABLE,
    ST_CAL_SPAN,
    ST_CAL_CONFLICT,
    ST_CAL_STORAGE_UNBOUND,
    ST_CAL_STORAGE_ERROR,
    ST_CAL_ZERO_EXPIRED,
    ST_CAL_COEFFICIENT
} st_cal_error;
typedef struct
{
    st_cal_profile candidate;
    st_cal_window window;
    st_cal_state state;
    st_cal_error error;
    uint64_t collect_us;
    uint32_t expected_active_crc;
    bool have_reference;
} st_cal_workflow;
typedef struct
{
    void *ctx;
    /* True ONLY after durable save + verification. On error retain old stored
     * parameters. Async storage/radio acceptance must NOT be reported as success. */
    bool (*save)(void *, const uint8_t profile[ST_CAL_PROFILE_BYTES]);
} st_cal_storage;
bool st_cal_binding_valid(st_cal_binding);
bool st_cal_binding_equal(st_cal_binding, st_cal_binding);
bool st_cal_reference_force(st_cal_reference, int64_t *nano_newtons);
size_t st_cal_profile_encode(const st_cal_profile *, uint8_t out[ST_CAL_PROFILE_BYTES]);
bool st_cal_profile_decode(st_cal_profile *, const uint8_t *, size_t);
bool st_cal_begin(st_cal_workflow *, st_cal_binding, st_cal_policy, const st_cal_profile *active,
                  bool idle, uint64_t now_us);
bool st_cal_start_zero(st_cal_workflow *, bool idle, uint64_t now_us);
bool st_cal_clear_reference(st_cal_workflow *);
bool st_cal_set_reference(st_cal_workflow *, st_cal_reference);
bool st_cal_start_load(st_cal_workflow *, bool idle, uint64_t now_us);
/* Read-only tap of original captures, foreground only. No raw array/record edits.
 * Samples must have the same monotonic clock as start/tick and contiguous DRDY.
 * Invalid CRC/status/saturation/gap/config changes fail the candidate only. */
bool st_cal_feed(st_cal_workflow *, st_cal_binding, const st_capture *);
void st_cal_tick(st_cal_workflow *, uint64_t now_us);
void st_cal_cancel(st_cal_workflow *);
bool st_cal_commit(st_cal_workflow *, st_cal_profile *active, st_cal_binding current, bool idle,
                   st_cal_storage);
/* Apply only an accepted profile to an IDLE logger with matching config.
 * On config mismatch clear calibration_valid, retaining coefficients/profile.
 * Historical files and raw capture bytes are never rewritten. */
bool st_cal_apply_logger(const st_cal_profile *, st_cal_binding current, st_logger *);
#endif
