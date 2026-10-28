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
            fprintf(stderr, "STOP line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define G(x)                                                                                       \
    do                                                                                             \
    {                                                                                              \
        groups++;                                                                                  \
        puts("PASS " x);                                                                           \
    } while (0)
typedef struct
{
    st_logger logger;
    st2_server server;
    st2_client client;
    st_ui ui;
    uint32_t now;
    uint64_t counter;
    st_file_op op;
    size_t bytes, length;
    unsigned opens, syncs, closes;
    bool hold_close, fail_sync, nv_fail;
    uint8_t file[2048];
} fixture;
static st_async_result submit(void *ctx, st_file_op op, const uint8_t *p, size_t n)
{
    fixture *f = ctx;
    f->op = op;
    f->bytes = n;
    if (op == ST_FILE_OPEN_NEW)
        f->opens++;
    if (op == ST_FILE_WRITE)
    {
        C(f->length + n <= sizeof f->file);
        memcpy(f->file + f->length, p, n);
        f->length += n;
    }
    if (op == ST_FILE_SYNC)
        f->syncs++;
    if (op == ST_FILE_CLOSE)
        f->closes++;
    return ST_IO_DONE;
}
static st_async_result poll(void *ctx, size_t *n)
{
    fixture *f = ctx;
    if (f->op == ST_FILE_CLOSE && f->hold_close)
        return ST_IO_WAIT;
    if (f->op == ST_FILE_SYNC && f->fail_sync)
        return ST_IO_ERROR;
    *n = f->bytes;
    return ST_IO_DONE;
}
static bool counter(void *ctx, uint64_t *out)
{
    fixture *f = ctx;
    if (f->nv_fail)
        return false;
    *out = ++f->counter;
    return true;
}
static fixture *init(void)
{
    fixture *f = calloc(1, sizeof *f);
    C(f);
    st_logger_init(&f->logger,
                   (st_log_config){.adc_clock_hz = ST_ADC_CLOCK_HZ,
                                   .osr = ST_ADC_OSR,
                                   .rate_millihz = ST_ADC_RATE_MILLIHZ,
                                   .boot_epoch = 7,
                                   .gain_ch0 = 1,
                                   .gain_ch1 = 0,
                                   .clock_verified = true},
                   (st_file_port){f, submit, poll});
    Log_ApplyAds1220Metadata(&f->logger.config, true, ST_ADC_RATE_MILLIHZ);
    st2_timing t = {10000, 150, 500, 120000, 4};
    st2_server_init(&f->server, &f->logger, (st2_nv_port){0}, t, (uint8_t[12]){1}, (uint8_t[12]){2},
                    0);
    f->server.offered = f->server.active = true;
    f->server.epoch = 7;
    st2_client_init(&f->client, (st2_nv_port){f, NULL, counter}, t, (uint8_t[12]){2},
                    (uint8_t[12]){1});
    f->client.link = ST2_LINK_UP;
    f->client.epoch = 7;
    f->client.have_status = true;
    f->client.next_command = 1;
    f->client.flags = ST_IDLE;
    st_ui_init(&f->ui);
    f->ui.page = ST_UI_CAPTURE;
    f->ui.selection = 2;
    return f;
}
static st2_measurement measurement(fixture *f)
{
    return (st2_measurement){.tf = f->logger.state == ST_IDLE ? ST2_TF_READY : ST2_TF_OPEN};
}
static st2_packet decode(const uint8_t *p, size_t n)
{
    st2_packet r;
    C(st2_decode(&r, p, n));
    return r;
}
static bool receive(fixture *f, st2_packet p)
{
    uint8_t wire[ST2_MAX];
    size_t n = st2_encode(&p, wire);
    C(n);
    return st2_client_receive(&f->client, wire, n, f->now);
}
static void status(fixture *f, unsigned state)
{
    C(receive(f, (st2_packet){.type = ST2_STATUS,
                              .epoch = f->client.epoch,
                              .id = (uint16_t)(f->client.status_sequence + 1),
                              .flags = (uint8_t)state}));
}
static void render(fixture *f, const char *expected)
{
    st_ui_model model;
    st_ui2_render(&f->ui, &f->client, (st_voltage_view){0}, f->now, &model);
    (void)expected;
    C(strstr(model.rows[1], Ui_Text("开始", "START")));
    for (unsigned row = 0; row < 4; row++)
    {
        unsigned width;
        C(Ui_ValidateText(model.rows[row], &width) == UI_TEXT_OK);
    }
}
static void advance(fixture *f, st_capture_state target)
{
    for (unsigned i = 0; i < 50 && f->logger.state != target; i++)
        st_logger_service(&f->logger, (uint64_t)++f->now * 1000);
    C(f->logger.state == target);
}
static void ordered_stop(unsigned mode)
{
    fixture *f = init();
    st2_client *c = &f->client;
    uint8_t start[ST2_MAX], stop[ST2_MAX], reply[ST2_MAX], scratch[ST2_MAX];
    size_t sn, n, rn;
    C(st2_client_command(c, ST2_START, f->now));
    C(st2_client_tx_due(c, f->now, start, &sn));
    st2_client_tx_accepted(c);
    st2_packet late = {.type = ST2_ACK,
                       .epoch = 7,
                       .id = c->request.id,
                       .flags = (uint8_t)(ST2_ACCEPTED | (ST_STARTING << 3))};
    if (mode != 0)
    {
        rn = st2_server_receive(&f->server, start, sn, measurement(f), 0, reply);
        C(rn == 21 && f->logger.state == ST_STARTING);
        if (mode == 2)
        {
            advance(f, ST_CAPTURING);
            st_capture sample = {.time_us = 500, .sequence = 1};
            C(st_logger_offer_isr(&f->logger, &sample));
            late.flags = (uint8_t)(ST2_COMPLETED | (ST_CAPTURING << 3) | (ST2_TF_OPEN << 6));
        }
        if (mode == 3)
            C(st_logger_stop(&f->logger)); /* Actual source-cancel path. */
    }
    C(st_ui2_handle(&f->ui, ST_UI_PRESS, c, ++f->now));
    C(c->stop == ST2_STOP_QUEUED && c->pending && c->request.type == ST2_START);
    render(f, "STOP QUEUED");
    uint32_t queued = c->stop_requested_ms;
    C(st_ui2_handle(&f->ui, ST_UI_PRESS, c, ++f->now));
    C(c->stop_requested_ms == queued && c->next_command == 2);
    C(!st2_client_tx_due(c, f->now, scratch, &n));
    C(c->request.id == 1 && c->waiting_tx_done);
    if (mode >= 2)
        C(receive(f, late)); /* Late ACCEPTED/COMPLETED cannot cancel stop intent. */
    C(c->stop == ST2_STOP_QUEUED);
    render(f, "STOP QUEUED");
    st2_client_tx_done(c, ++f->now, 0);
    C(st2_client_tx_due(c, f->now, stop, &n));
    C(c->stop == ST2_STOP_ACTIVE && c->request.type == ST2_STOP && c->request.id == 2);
    C(c->started_ms == queued);
    C(!receive(f, late)); /* Wrong command identity cannot complete STOP. */
    st2_client_tx_accepted(c);
    st2_client_tx_done(c, ++f->now, 0);
    rn = st2_server_receive(&f->server, stop, n, measurement(f), (uint64_t)f->now * 1000, reply);
    C(rn == 21 && f->server.last_command == 2 && f->server.last_type == ST2_STOP);
    unsigned generation = f->logger.generation;
    uint32_t deadline = c->started_ms;
    C(st2_client_command(c, ST2_STOP, ++f->now));
    C(c->request.id == 2 && c->started_ms == deadline && c->attempts == 1);
    C(st2_client_receive(c, reply, rn, f->now));
    if (mode)
    {
        C(c->pending && c->transaction == ST_TX_WAIT_STATE);
        render(f, "WAIT STOP CONFIRM");
        C(!atomic_load(&f->logger.capture_enabled));
        f->hold_close = true;
        for (unsigned i = 0; i < 30; i++)
            st_logger_service(&f->logger, (uint64_t)++f->now * 1000);
        C(f->logger.state == ST_STOPPING && f->closes == 1 && f->syncs == 2);
        rn =
            st2_server_receive(&f->server, stop, n, measurement(f), (uint64_t)f->now * 1000, reply);
        C((decode(reply, rn).flags & 7u) == ST2_ACCEPTED);
        C(c->pending && f->logger.generation == generation);
        f->hold_close = false;
        advance(f, ST_IDLE);
        rn = st2_server_poll(&f->server, measurement(f), (uint64_t)f->now * 1000, reply);
        C((decode(reply, rn).flags & 7u) == ST2_COMPLETED);
        /* Lose terminal ACK: IDLE status alone never claims confirmed closure. */
        status(f, ST_IDLE);
        C(c->pending && c->stop == ST2_STOP_ACTIVE);
        C(!st2_client_command(c, ST2_START, f->now));
        f->now += 500;
        C(st2_client_tx_due(c, f->now, scratch, &rn));
        C(rn == n && !memcmp(scratch, stop, n));
        rn =
            st2_server_receive(&f->server, stop, n, measurement(f), (uint64_t)f->now * 1000, reply);
        C(st2_client_receive(c, reply, rn, f->now));
        C(f->closes == 1 && f->opens == 1);
        C(st_get16(f->file + f->length - 32 + 22) == ST_REC_END);
    }
    C(!c->pending && c->stop == ST2_STOP_NONE && c->transaction == ST_TX_COMPLETE);
    rn = st2_server_receive(&f->server, start, sn, measurement(f), (uint64_t)f->now * 1000, reply);
    C((decode(reply, rn).flags & 7u) != ST2_ACCEPTED);
    C(f->logger.state == ST_IDLE && f->logger.generation == generation);
    status(f, ST_IDLE);
    render(f, "STATE CONFIRMED");
    C(st2_client_command(c, ST2_START, ++f->now));
    C(c->request.id == 3);
    free(f);
}
static void ordering(void)
{
    for (unsigned mode = 0; mode < 4; mode++)
        ordered_stop(mode);
    G("STOP ordering: lost/late START ACK, STOP before delayed START, STARTING/REC/source-cancel, "
      "same-ID retries, held close, lost terminal ACK, IDLE status cannot confirm, explicit "
      "restart");
}
static void failures(void)
{
    fixture *f = init();
    st2_client *c = &f->client;
    uint8_t wire[ST2_MAX], out[ST2_MAX];
    size_t n, m;
    C(st2_client_command(c, ST2_START, 0));
    n = st2_encode(&c->request, wire);
    C(st2_server_receive(&f->server, wire, n, measurement(f), 0, out) == 21);
    C(st2_client_command(c, ST2_STOP, 1));
    C(st2_client_tx_due(c, 1, wire, &n));
    C(st2_server_receive(&f->server, wire, n, measurement(f), 1000, out) == 21);
    f->fail_sync = true;
    advance(f, ST_FAULT);
    m = st2_server_poll(&f->server, measurement(f), (uint64_t)f->now * 1000, out);
    C((decode(out, m).flags & 7u) == ST2_FAILED);
    C(st2_client_receive(c, out, m, f->now));
    C(c->stop == ST2_STOP_UNCONFIRMED && !c->pending && c->transaction == ST_TX_REJECTED);
    render(f, "STOP FAILED / CHECK");
    C(!st2_client_command(c, ST2_START, f->now));
    C(f->closes == 0);
    free(f);
    G("STOP sync failure reports FAILED, never CLOSED or restart-ready");
}
static void uncertainty(void)
{
    fixture *f = init();
    st2_client *c = &f->client;
    uint8_t wire[ST2_MAX], out[ST2_MAX];
    size_t n, m;
    C(st2_client_command(c, ST2_STOP, 0)); /* Already IDLE still needs matching ACK. */
    uint32_t id = c->request.id;
    for (unsigned i = 0; i < 4; i++)
    {
        f->now = i * 500;
        C(st2_client_tx_due(c, f->now, wire, &n));
        st2_client_tx_accepted(c);
        st2_client_tx_done(c, f->now, 0);
        m = st2_server_receive(&f->server, wire, n, measurement(f), (uint64_t)f->now * 1000, out);
        C((decode(out, m).flags & 7u) == ST2_COMPLETED); /* All ACKs lost. */
    }
    C(!st2_client_tx_due(c, 2000, wire, &n) && c->attempts == 4);
    f->now = 120001;
    C(!st2_client_tx_due(c, f->now, wire, &n));
    C(!c->pending && c->stop == ST2_STOP_UNCONFIRMED);
    render(f, "STOP UNCONFIRMED");
    status(f, ST_IDLE);
    C(!st2_client_command(c, ST2_START, f->now));
    C(!st2_client_tx_due(c, f->now, wire, &n)); /* No automatic recovery/replay. */
    C(st2_client_command(c, ST2_STOP, f->now));
    C(c->request.id == id && c->attempts == 0);
    C(st2_client_tx_due(c, f->now, wire, &n));
    m = st2_server_receive(&f->server, wire, n, measurement(f), (uint64_t)f->now * 1000, out);
    C(st2_client_receive(c, out, m, f->now));
    C(c->stop == ST2_STOP_NONE && c->transaction == ST_TX_COMPLETE);
    C(f->opens == 0 && f->closes == 0);
    C(st2_client_command(c, ST2_START, f->now));
    free(f);
    f = init();
    c = &f->client;
    C(st2_client_command(c, ST2_START, 0));
    f->now = 120001;
    C(!st2_client_tx_due(c, f->now, wire, &n) && c->transaction == ST_TX_TIMEOUT);
    status(f, ST_CAPTURING); /* START may have executed despite every lost ACK. */
    C(st2_client_command(c, ST2_STOP, f->now) && c->request.id == 2);
    C(c->pending && c->stop == ST2_STOP_ACTIVE);
    free(f);
    G("STOP bounded retries/timeout, already-IDLE barrier, retained uncertainty blocks START, "
      "explicit same-ID retry recovers lost completion");
}
static void freshness_and_cancel(void)
{
    fixture *stale = init();
    C(st2_client_command(&stale->client, ST2_START, 0));
    stale->now = 40000;
    C(st_ui2_handle(&stale->ui, ST_UI_PRESS, &stale->client, stale->now));
    C(stale->client.stop == ST2_STOP_QUEUED);
    render(stale, "LINK LOST / STOP ?");
    free(stale);
    fixture *f = init();
    st2_client *c = &f->client;
    uint8_t wire[ST2_MAX];
    size_t n;
    C(st2_client_command(c, ST2_START, 0));
    C(st2_client_command(c, ST2_STOP, 1));
    f->now = 31151;
    C(!st2_client_tx_due(c, f->now, wire, &n));
    C(c->request.type == ST2_START && c->stop == ST2_STOP_QUEUED);
    render(f, "LINK LOST / STOP ?");
    status(f, ST_STARTING);
    C(st2_client_tx_due(c, f->now, wire, &n) && c->request.type == ST2_STOP);
    C(!receive(f, (st2_packet){.type = ST2_ACK,
                               .epoch = 7,
                               .id = c->request.id,
                               .flags = (uint8_t)(ST2_COMPLETED | (ST_CAPTURING << 3))}));
    C(c->pending && c->stop == ST2_STOP_ACTIVE);
    free(f);
    f = init();
    c = &f->client;
    c->received_ms = UINT32_MAX - 10;
    C(st2_client_command(c, ST2_START, UINT32_MAX - 10));
    C(st2_client_command(c, ST2_STOP, UINT32_MAX - 9));
    c->waiting_tx_done = true;
    C(!st2_client_tx_due(c, 20, wire, &n) && c->stop == ST2_STOP_QUEUED);
    C(!st2_client_tx_due(c, 120000, wire, &n));
    C(c->stop == ST2_STOP_UNCONFIRMED && !c->pending);
    f->now = 120001;
    status(f, ST_IDLE);
    C(!st2_client_command(c, ST2_START, f->now));
    f->nv_fail = true;
    C(st2_client_connect(c, f->now) && c->stop == ST2_STOP_UNCONFIRMED);
    f->nv_fail = false;
    C(st2_client_connect(c, f->now) && c->stop == ST2_STOP_UNCONFIRMED && !c->pending);
    C(c->request.type == ST2_HELLO && !c->have_status);
    C(st2_client_tx_due(c, f->now, wire, &n) && wire[2] == ST_WIRE_HELLO);
    free(f);
    G("STOP fresh-state gate, queue expiry across timer wrap, malformed completion rejected, "
      "failed reconnect retains intent, explicit reconnect cancels without START replay");
}
static void ui_messages(void)
{
    fixture *f = init();
    f->client.task = (StTask){ST_TASK_CAPTURE, ST_TASK_RUNNING, 1, 0};
    f->ui.selection = 0;
    C(!st_ui2_handle(&f->ui, ST_UI_PRESS, &f->client, 0));
    C(f->ui.page == ST_UI_HOME && f->client.task.state == ST_TASK_RUNNING && !f->client.pending);
    free(f);
    G("R8 BACK navigation preserves recording task");
}
unsigned st_test_stop(unsigned *out)
{
    ordering();
    failures();
    uncertainty();
    freshness_and_cancel();
    ui_messages();
    *out += groups;
    return checks;
}
