#include "st_cal_endpoint.h"
#include "st_reference.h"
#include "st_cal_remote.h"
#include "st_cal_ui.h"
#include "st_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, groups;
#define C(x)                                                                                       \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "cal integration line%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define G(s)                                                                                       \
    do                                                                                             \
    {                                                                                              \
        groups++;                                                                                  \
        puts("PASS " s);                                                                           \
    } while (0)
st_cal_profile st_cal_test_profile(void);
typedef struct
{
    st_cal_profile active;
    st_cal_binding binding;
    st_cal_service service;
    st_cal_endpoint endpoint;
    st_logger logger;
    st2_server server;
    st2_client client;
    st_cal_remote remote;
    uint64_t now;
    unsigned saves;
    bool fail_save;
} fixture;
static uint64_t now_us(void *ctx)
{
    return ((fixture *)ctx)->now;
}
static uint32_t now_ms(void *ctx)
{
    return (uint32_t)(((fixture *)ctx)->now / 1000);
}
static bool idle(void *ctx)
{
    fixture *f = ctx;
    return f->logger.state == ST_IDLE && !atomic_load(&f->logger.capture_enabled);
}
static bool save(void *ctx, const uint8_t p[256])
{
    fixture *f = ctx;
    st_cal_profile parsed;
    f->saves++;
    C(st_cal_profile_decode(&parsed, p, 256));
    C(parsed.revision == f->active.revision + 1);
    return !f->fail_save;
}
static void init(fixture *f)
{
    *f = (fixture){.now = 1000000};
    f->active = st_cal_test_profile();
    f->binding = f->active.binding;
    st_log_config cfg = {.adc_clock_hz = f->binding.adc_clock_hz,
                         .osr = f->binding.osr,
                         .rate_millihz = f->binding.rate_millihz,
                         .gain_ch0 = f->binding.gain0,
                         .gain_ch1 = f->binding.gain1,
                         .clock_verified = true,
                         .simulated = true};
    st_logger_init(&f->logger, cfg, (st_file_port){0});
    st_cal_service_init(&f->service, &f->active, &f->binding, f->active.policy,
                        (st_cal_storage){f, save}, f, now_us, idle);
    st_cal_endpoint_init(&f->endpoint, &f->service, &f->logger);
    st2_timing timing = {10000, 2200, 2500, 100000, 4};
    st2_server_init(&f->server, &f->logger, (st2_nv_port){0}, timing, (uint8_t[12]){1},
                    (uint8_t[12]){2}, 0);
    f->server.active = true;
    f->server.offered = true;
    f->server.epoch = 23;
    st2_server_attach_calibration(&f->server, st_cal_endpoint_port(&f->endpoint));
    st2_client_init(&f->client, (st2_nv_port){0}, timing, (uint8_t[12]){2}, (uint8_t[12]){1});
    f->client.link = ST2_LINK_UP;
    f->client.epoch = 23;
    f->client.have_status = true;
    f->client.received_ms = now_ms(f);
    f->client.next_command = 1;
    f->client.flags = ST_IDLE;
    st_cal_remote_init(&f->remote, &f->client, f, now_ms);
}
static st_capture capture(int32_t raw, uint32_t seq, uint64_t now)
{
    st_capture c = {.time_us = now, .sequence = seq};
    c.bytes[0] = 1;
    c.bytes[1] = 3;
    c.bytes[3] = (uint8_t)((uint32_t)raw >> 16);
    c.bytes[4] = (uint8_t)((uint32_t)raw >> 8);
    c.bytes[5] = (uint8_t)raw;
    uint16_t crc = st_crc16(c.bytes, 9);
    c.bytes[9] = (uint8_t)(crc >> 8);
    c.bytes[10] = (uint8_t)crc;
    return c;
}
static void feed(fixture *f, int32_t raw, uint32_t seq)
{
    f->now += f->active.policy.settle_us;
    for (unsigned i = 0; i < 16; i++)
    {
        st_capture c = capture(raw, seq + i, f->now), old = c;
        C(st_cal_input_offer_isr(&f->endpoint.input, &c));
        C(!memcmp(&old, &c, sizeof(c)));
        f->now += 500;
    }
    st_cal_endpoint_step(&f->endpoint);
}
static void request(fixture *f, st_cal_action action, const st_cal_reference *ref)
{
    st_cal_control_port p = st_cal_remote_port(&f->remote);
    C(p.request(p.ctx, action, ref));
}
static void exchange(fixture *f, bool duplicate)
{
    uint8_t in[ST2_MAX], out[4][ST2_MAX];
    size_t lens[4];
    size_t n = st2_encode(&f->client.request, in);
    C(n == 18 || n == 32);
    lens[0] = st2_server_receive(&f->server, in, n, (st2_measurement){0}, f->now, out[0]);
    C(lens[0] == 35);
    for (unsigned i = 1; i < 4; i++)
    {
        lens[i] = st2_server_poll(&f->server, (st2_measurement){0}, f->now, out[i]);
        C(lens[i] == 35);
    }
    if (duplicate)
    {
        unsigned saves = f->saves;
        uint8_t retry[ST2_MAX];
        C(st2_server_receive(&f->server, in, n, (st2_measurement){0}, f->now, retry) == 35);
        C(!memcmp(out[0], retry, 35) && f->saves == saves);
        /* The retry's pending fragments do not mutate the cached snapshot. */
    }
    C(st2_client_receive(&f->client, out[3], lens[3], now_ms(f)));
    C(st2_client_receive(&f->client, out[1], lens[1], now_ms(f)));
    C(st2_client_receive(&f->client, out[1], lens[1], now_ms(f)));
    C(f->client.pending && !f->client.cal_have_view);
    uint8_t bad[ST2_MAX];
    memcpy(bad, out[1], 26);
    bad[9] ^= 1;
    st_put16(bad + 24, st_crc16(bad, 24));
    C(!st2_client_receive(&f->client, bad, 26, now_ms(f)));
    C(st2_client_receive(&f->client, out[0], lens[0], now_ms(f)));
    C(st2_client_receive(&f->client, out[2], lens[2], now_ms(f)));
    C(!f->client.pending && f->client.cal_have_view && f->client.transaction == ST_TX_COMPLETE);
}
static st_cal_view view(fixture *f)
{
    st_cal_view v;
    st_cal_control_port p = st_cal_remote_port(&f->remote);
    C(p.snapshot(p.ctx, &v));
    return v;
}
static void query(fixture *f)
{
    C(st2_client_cal_command(&f->client, NULL, now_ms(f)));
    exchange(f, false);
}
static void prepare(fixture *f)
{
    request(f, ST_CAL_ACTION_BEGIN, NULL);
    exchange(f, true);
    C(view(f).state == ST_CAL_ZERO_READY);
    /* All START paths, including a direct wire request, respect the wizard lock. */
    C(st2_client_command(&f->client, ST2_START, now_ms(f)));
    uint8_t in[ST2_MAX], out[ST2_MAX];
    size_t n = st2_encode(&f->client.request, in);
    n = st2_server_receive(&f->server, in, n, (st2_measurement){0}, f->now, out);
    C(n == 21 && st2_client_receive(&f->client, out, n, now_ms(f)) &&
      f->client.transaction == ST_TX_REJECTED);
    C(f->logger.state == ST_IDLE);
    request(f, ST_CAL_ACTION_ZERO, NULL);
    exchange(f, true);
    feed(f, 1000, 10);
    query(f);
    C(view(f).state == ST_CAL_LOAD_READY);
    st_cal_reference ref = {ST_CAL_GRAMS, 100000, ST_CAL_G0_UM_S2};
    request(f, ST_CAL_ACTION_REFERENCE, &ref);
    exchange(f, true);
    request(f, ST_CAL_ACTION_LOAD, NULL);
    exchange(f, true);
    feed(f, 2000, 100);
    query(f);
    C(view(f).state == ST_CAL_REVIEW && f->active.revision == 1);
}
static void input_tests(void)
{
    fixture *f = malloc(sizeof(*f));
    C(f != NULL);
    init(f);
    st_cal_control_port p = st_cal_service_port(&f->service);
    C(p.request(p.ctx, ST_CAL_ACTION_BEGIN, NULL));
    C(p.request(p.ctx, ST_CAL_ACTION_ZERO, NULL));
    st_cal_endpoint_step(&f->endpoint);
    atomic_store(&f->endpoint.input.head, UINT32_MAX - 3);
    atomic_store(&f->endpoint.input.tail, UINT32_MAX - 3);
    st_capture stale = capture(9000, 1, f->now - 1);
    C(st_cal_input_offer_isr(&f->endpoint.input, &stale));
    feed(f, 1000, 10);
    C(f->service.workflow.state == ST_CAL_LOAD_READY &&
      f->service.workflow.candidate.zero_raw == 1000);
    C(!atomic_load(&f->endpoint.input.enabled));
    C(!st_cal_input_offer_isr(&f->endpoint.input, &stale));
    C(p.request(p.ctx, ST_CAL_ACTION_BEGIN, NULL));
    C(p.request(p.ctx, ST_CAL_ACTION_ZERO, NULL));
    st_cal_endpoint_step(&f->endpoint);
    for (unsigned i = 0; i < 64; i++)
    {
        st_capture c = capture(1000, i, f->now + 3000 + i * 500);
        C(st_cal_input_offer_isr(&f->endpoint.input, &c));
    }
    C(!st_cal_input_offer_isr(&f->endpoint.input, &stale));
    st_cal_endpoint_step(&f->endpoint);
    C(f->service.workflow.state == ST_CAL_FAILED && f->service.workflow.error == ST_CAL_SEQUENCE &&
      f->active.revision == 1);
    C(p.request(p.ctx, ST_CAL_ACTION_BEGIN, NULL));
    C(p.request(p.ctx, ST_CAL_ACTION_ZERO, NULL));
    st_cal_endpoint_step(&f->endpoint);
    st_cal_input_error_isr(&f->endpoint.input);
    st_cal_endpoint_step(&f->endpoint);
    C(f->service.workflow.state == ST_CAL_FAILED && f->active.revision == 1 && f->saves == 0);
    f->logger.state = ST_CAPTURING;
    C(!p.request(p.ctx, ST_CAL_ACTION_BEGIN, NULL));
    C(f->active.revision == 1);
    free(f);
    G("cal real-DMA tap queue / raw bytes unchanged / stale edge discard / uint32 wrap / bounded "
      "drain / overflow and DMA-error reject candidate / capture busy preserves active");
}
static void rpc_tests(void)
{
    fixture *f = malloc(sizeof(*f));
    C(f != NULL);
    init(f);
    prepare(f);
    request(f, ST_CAL_ACTION_SAVE, NULL);
    uint8_t command[ST2_MAX], bad[ST2_MAX], reply[ST2_MAX];
    size_t n = st2_encode(&f->client.request, command);
    for (size_t i = 0; i < n; i++)
    {
        memcpy(bad, command, n);
        bad[i] ^= 1;
        C(st2_server_receive(&f->server, bad, n, (st2_measurement){0}, f->now, reply) == 0);
    }
    for (size_t i = 0; i < n; i++)
        C(st2_server_receive(&f->server, command, i, (st2_measurement){0}, f->now, reply) == 0);
    C(f->saves == 0);
    exchange(f, true);
    C(f->saves == 1 && f->active.revision == 2 && view(f).state == ST_CAL_COMMITTED);
    st2_packet conflict = f->client.request;
    conflict.data[0] = ST_CAL_ACTION_CANCEL;
    n = st2_encode(&conflict, bad);
    n = st2_server_receive(&f->server, bad, n, (st2_measurement){0}, f->now, reply);
    st2_packet ack;
    C(n == 21 && st2_decode(&ack, reply, n) && (ack.flags & 7u) == ST2_CONFLICT &&
      f->active.revision == 2);
    conflict.epoch++;
    n = st2_encode(&conflict, bad);
    C(!st2_server_receive(&f->server, bad, n, (st2_measurement){0}, f->now, reply));
    C(f->server.calibration.prepare_start(f->server.calibration.ctx));
    C(f->logger.config.calibration_valid && f->logger.config.profile_version == 2);
    f->binding.gain0 = 64;
    C(f->server.calibration.prepare_start(f->server.calibration.ctx));
    C(!f->logger.config.calibration_valid);
    f->binding.gain0 = 128;
    request(f, ST_CAL_ACTION_BEGIN, NULL);
    exchange(f, false);
    f->now += (uint64_t)(f->server.timing.operation_ms + 1) * 1000;
    (void)st2_server_poll(&f->server, (st2_measurement){0}, f->now, reply);
    C(f->service.workflow.state == ST_CAL_CANCELLED && f->active.revision == 2);
    free(f);
    G("R8 cal 32B command / 18B query / four35B frozen reply fragments / reorder-duplicate-conflict / "
      "CRC-truncation / old epoch / save executes once / START lock and apply / session lease "
      "cancel");
}
static void uncertainty_tests(void)
{
    fixture *f = malloc(sizeof(*f));
    C(f != NULL);
    init(f);
    prepare(f);
    request(f, ST_CAL_ACTION_SAVE, NULL);
    uint8_t wire[ST2_MAX], reply[ST2_MAX];
    size_t n = st2_encode(&f->client.request, wire);
    C(st2_server_receive(&f->server, wire, n, (st2_measurement){0}, f->now, reply) == 35);
    C(f->active.revision == 2 && f->saves == 1); /* all replies lost */
    st2_client_tx_accepted(&f->client);
    st2_client_tx_done(&f->client, now_ms(f), 0);
    C(!st2_client_tx_due(&f->client, now_ms(f) + f->client.timing.ack_rto_ms, wire, &n));
    C(st2_client_tx_due(
        &f->client, now_ms(f) + f->client.timing.ack_rto_ms + 3u * f->client.timing.max_airtime_ms,
        wire, &n));
    st_cal_ui ui;
    st_cal_ui_init(&ui, st_cal_remote_port(&f->remote));
    ui.page = ST_CAL_UI_RESULT;
    st_cal_panel panel;
    st_cal_ui_render(&ui, &panel);
    C(!strcmp(panel.rows[1], Ui_Text("等待匹配结果", "WAIT MATCHED RESULT")));
    f->now += (uint64_t)(f->client.timing.operation_ms + 1) * 1000;
    C(!st2_client_tx_due(&f->client, now_ms(f), wire, &n));
    C(view(f).uncertain);
    (void)st2_server_poll(&f->server, (st2_measurement){0}, f->now, reply);
    C(f->service.workflow.state == ST_CAL_COMMITTED);
    st_cal_ui_render(&ui, &panel);
    C(!strcmp(panel.rows[1], Ui_Text("结果未确认", "RESULT UNKNOWN")));
    C(strcmp(panel.rows[1], "OLD PROFILE RETAINED"));
    f->client.received_ms = now_ms(f);
    query(f);
    C(view(f).state == ST_CAL_COMMITTED && !view(f).uncertain);
    st_cal_ui_render(&ui, &panel);
    C(!strcmp(panel.rows[1], Ui_Text("标定已保存", "CALIBRATION SAVED")));
    /* Rejected capture-time SAVE cannot mutate the accepted profile. */
    f->client.flags = ST_IDLE;
    f->logger.state = ST_CAPTURING;
    request(f, ST_CAL_ACTION_BEGIN, NULL);
    exchange(f, false);
    C(view(f).request_failed && view(f).error == ST_CAL_BUSY && f->active.revision == 2);
    free(f);
    G("cal remote menu waits for complete reply / lost SAVE reply yields RESULT UNKNOWN / explicit "
      "query reconciles durable result / capture-time mutation rejected");
}

static void background_query_test(void)
{
    fixture *f = malloc(sizeof(*f));
    C(f != NULL);
    init(f);
    request(f, ST_CAL_ACTION_BEGIN, NULL);
    exchange(f, false);
    C(view(f).state == ST_CAL_ZERO_READY);
    C(st2_client_cal_command(&f->client, NULL, now_ms(f)));
    uint32_t query_id = f->client.request.id;
    C(!view(f).pending && view(f).state == ST_CAL_ZERO_READY);
    st_cal_ui ui;
    st_cal_ui_init(&ui, st_cal_remote_port(&f->remote));
    ui.page = ST_CAL_UI_EDITOR;
    ui.selection = 0;
    C(!st_cal_ui_handle(&ui, ST_CAL_UI_NEXT, true) && ui.selection == 1);
    request(f, ST_CAL_ACTION_CANCEL, NULL);
    C(f->client.request.id > query_id);
    exchange(f, false);
    C(view(f).state == ST_CAL_CANCELLED && f->active.revision == 1);
    free(f);
    G("cal background read-only query keeps encoder responsive / explicit action supersedes poll / "
      "previous cached outcome cannot complete a new command");
}

typedef struct
{
    size_t completed;
    unsigned headers, closes;
    uint8_t header[512];
} start_file;
static st_async_result start_submit(void *ctx, st_file_op op, const uint8_t *p, size_t n)
{
    start_file *file = ctx;
    file->completed = n;
    if (op == ST_FILE_WRITE && n == 512 && !memcmp(p, "STLOG001", 8))
    {
        memcpy(file->header, p, 512);
        file->headers++;
    }
    if (op == ST_FILE_CLOSE)
        file->closes++;
    return ST_IO_DONE;
}
static st_async_result start_poll(void *ctx, size_t *n)
{
    *n = ((start_file *)ctx)->completed;
    return ST_IO_DONE;
}
static bool application_prepare(void *ctx)
{
    fixture *f = ctx;
    return st_ref_prepare_start(&f->logger, st_cal_endpoint_port(&f->endpoint), f->server.epoch,
                                true, true);
}
static void application_command(fixture *f, uint8_t type, uint16_t id)
{
    st2_packet request = {.type = type, .epoch = f->server.epoch, .id = id};
    uint8_t in[ST2_MAX], out[ST2_MAX];
    size_t n = st2_encode(&request, in);
    size_t got = st2_server_receive(&f->server, in, n, (st2_measurement){0}, f->now, out);
    st2_packet reply;
    C(got == 21 && st2_decode(&reply, out, got));
    if ((reply.flags & 7u) != ST2_ACCEPTED)
        fprintf(stderr, "reference command type=%u id=%u result=%u state=%u\n", type, id,
                reply.flags & 7u, (unsigned)f->logger.state);
    C((reply.flags & 7u) == ST2_ACCEPTED);
}
static void application_advance(fixture *f, st_capture_state state)
{
    for (unsigned i = 0; i < 100 && f->logger.state != state; i++)
    {
        f->now += 1000;
        st_logger_service(&f->logger, f->now);
        uint8_t response[ST2_MAX];
        (void)st2_server_poll(&f->server, (st2_measurement){0}, f->now, response);
    }
    C(f->logger.state == state);
}
static void check_saved_header(fixture *f)
{
    C(f->logger.header[59] & 1u);
    C(st_get32(f->logger.header + 24) == f->server.epoch);
    C(st_get32(f->logger.header + 28) == f->active.binding.profile_id);
    C(st_get32(f->logger.header + 32) == f->active.revision);
    C((int32_t)st_get32(f->logger.header + 36) == f->active.zero_raw);
    C((int64_t)st_get64(f->logger.header + 40) == f->active.nano_newtons_per_count);
    C(f->logger.header[58] == f->active.binding.wire_count);
    C(st_get32(f->logger.header + 508) == st_crc32(f->logger.header, 508));
}
static void first_application_start(void)
{
    fixture *f = calloc(1, sizeof *f);
    C(f);
    init(f);
    start_file file = {0};
    f->logger.port = (st_file_port){&file, start_submit, start_poll};
    f->logger.config.clock_verified = false;
    f->logger.config.calibration_valid = false;
    f->server.calibration = (st2_cal_port){.ctx = f, .prepare_start = application_prepare};
    st_cal_profile saved = f->active;
    C(!st_ref_prepare_start(&f->logger, st_cal_endpoint_port(&f->endpoint), 23, false, true));
    C(!f->logger.config.clock_verified && !f->logger.config.calibration_valid);
    application_command(f, ST2_START, 1);
    check_saved_header(f); /* Red with old application order. */
    application_advance(f, ST_CAPTURING);
    C(file.headers == 1);
    C(!st_ref_prepare_start(&f->logger, st_cal_endpoint_port(&f->endpoint), 24, true, true));
    application_command(f, ST2_STOP, 2);
    application_advance(f, ST_IDLE);
    C(file.closes == 1);
    st_cal_control_port control = st_cal_service_port(&f->service);
    C(control.request(control.ctx, ST_CAL_ACTION_BEGIN, NULL));
    C(!application_prepare(f)); /* A wizard cannot be bypassed by clock confirmation. */
    C(control.request(control.ctx, ST_CAL_ACTION_CANCEL, NULL));
    C(!memcmp(&saved, &f->active, sizeof saved));
    f->logger.config.clock_verified = false;
    application_command(f, ST2_START, 3);
    check_saved_header(f);
    /* Source-fault/reconnect cancellation in the real board calls logger_stop.
     * RF commands are serialized; a second command during pending START is rejected. */
    C(st_logger_stop(&f->logger));
    application_advance(f, ST_IDLE);
    C(file.headers == 2 && file.closes == 2);
    f->logger.config.clock_verified = false;
    application_command(f, ST2_START, 5);
    check_saved_header(f);
    application_advance(f, ST_CAPTURING);
    application_command(f, ST2_STOP, 6);
    application_advance(f, ST_IDLE);
    C(file.headers == 3 && file.closes == 3 && !memcmp(&saved, &f->active, sizeof saved));
    C(st_ref_prepare_start(&f->logger, (st2_cal_port){0}, 24, true, false));
    C(!f->logger.config.calibration_valid && f->logger.config.clock_verified);
    C(st_logger_start(&f->logger, f->now));
    C(!(f->logger.header[59] & 1u));
    C(st_logger_stop(&f->logger));
    application_advance(f, ST_IDLE);
    free(f);
    G("actual reference START preparation: saved coefficients in first STLOG header, source guard, "
      "wizard cancel, source cancel during STARTING, restart and raw-only path");
}
static void session_zero_tests(void)
{
    fixture *f = malloc(sizeof *f);
    C(f);
    init(f);
    st_cal_profile permanent = f->active;
    st2_cal_port port = st_cal_endpoint_port(&f->endpoint);
    uint8_t enable[14] = {8, 1};
    C(port.request(port.ctx, enable));
    port.begin_start(port.ctx);
    C(!port.prepare_start(port.ctx) && port.start_waiting(port.ctx));
    for (uint32_t seq = 0; seq < ST_ZERO_WINDOW_SAMPLES; seq++)
    {
        f->now += 500;
        st_capture c = capture(2222, seq, f->now), saved = c;
        C(st_cal_input_offer_isr(&f->endpoint.input, &c));
        st_cal_endpoint_step(&f->endpoint);
        C(!memcmp(&c, &saved, sizeof c));
    }
    C(f->endpoint.zero.state == ST_ZERO_READY && port.prepare_start(port.ctx));
    C(f->logger.config.session_zero_valid && f->logger.config.session_zero_raw == 2222 &&
      f->logger.config.zero_raw == 2222);
    C(f->logger.config.calibration_zero_raw == permanent.zero_raw &&
      f->logger.config.nano_newtons_per_count == permanent.nano_newtons_per_count);
    C(!memcmp(&f->active, &permanent, sizeof permanent) && !f->saves);
    start_file file = {0};
    f->logger.port = (st_file_port){&file, start_submit, start_poll};
    f->logger.config.boot_epoch = 23;
    C(st_logger_start(&f->logger, f->now));
    C(!memcmp(f->logger.header + 60, "SZR1", 4));
    C(st_get32(f->logger.header + 64) == (uint32_t)permanent.zero_raw &&
      st_get32(f->logger.header + 68) == 2222);
    C(st_get32(f->logger.header + 72) == ST_ZERO_WINDOW_SAMPLES);
    C(SessionZero_Apply(&f->endpoint.zero, &f->logger) == ST_ZERO_BUSY);
    /* A new start with auto-zero off restores permanent calibration. */
    f->logger.state = ST_IDLE;
    atomic_store(&f->logger.capture_enabled, false);
    f->logger.inflight = false;
    port.reset(port.ctx);
    enable[1] = 0;
    C(port.request(port.ctx, enable));
    port.begin_start(port.ctx);
    C(port.prepare_start(port.ctx) && !f->logger.config.session_zero_valid &&
      f->logger.config.zero_raw == permanent.zero_raw);
    uint8_t zero[14] = {7};
    C(port.request(port.ctx, zero));
    f->now += 500;
    st_capture c = capture(2222, 1, f->now);
    C(st_cal_input_offer_isr(&f->endpoint.input, &c));
    f->now += 500;
    c = capture(2222, 3, f->now);
    C(st_cal_input_offer_isr(&f->endpoint.input, &c));
    st_cal_endpoint_step(&f->endpoint);
    C(port.start_failed(port.ctx));
    port.reset(port.ctx);
    C(port.request(port.ctx, zero));
    f->now += (ST_ZERO_TIMEOUT_MS + 1u) * UINT64_C(1000);
    st_cal_endpoint_step(&f->endpoint);
    C(port.start_failed(port.ctx));
    port.reset(port.ctx);
    C(f->endpoint.zero.state == ST_ZERO_IDLE && !memcmp(&permanent, &f->active, sizeof permanent));
    free(f);
    G("R8 stable512 session zero / raw unchanged / permanent slope-zero isolated / SZR1 compatible "
      "header / no running adjustment / off restores calibration / gap-timeout-cancel");
}
unsigned st_test_cal_integration(unsigned *out)
{
    session_zero_tests();
    first_application_start();
    input_tests();
    rpc_tests();
    uncertainty_tests();
    background_query_test();
    *out += groups;
    return checks;
}
