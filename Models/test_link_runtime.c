#include "st_link_runtime.h"
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
            fprintf(stderr, "runtime line%d: %s\n", __LINE__, #x);                                 \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define G(s)                                                                                       \
    do                                                                                             \
    {                                                                                              \
        groups++;                                                                                  \
        puts("PASS " s);                                                                           \
    } while (0)
/* Software RF event model. No SPI/GPIO/card/radio device is opened. The actual
 * st_radio state machine and unmodified Semtech encoding run against this model. */
typedef struct
{
    uint8_t tx[49], rx[49], tx_n, rx_n;
    uint16_t irq;
    unsigned writes, seen;
    bool ready;
} bus;
static bool command(void *ctx, const uint8_t *cmd, uint16_t cn, const uint8_t *tx, uint8_t *rx,
                    uint16_t n)
{
    bus *b = ctx;
    C(cn && cn <= 16 && !(tx && rx));
    if (rx)
        memset(rx, 0, n);
    if (cmd[0] == 0x15 && rx && n == 2)
    {
        rx[0] = (uint8_t)(b->irq >> 8);
        rx[1] = (uint8_t)b->irq;
    }
    if (cmd[0] == 0x97)
        b->irq = 0;
    if (cmd[0] == 0x1a && tx)
    {
        C(n <= 49);
        memcpy(b->tx, tx, n);
        b->tx_n = (uint8_t)n;
        b->writes++;
    }
    if (cmd[0] == 0x17 && rx && n == 2)
    {
        rx[0] = b->rx_n;
        rx[1] = 128;
    }
    if (cmd[0] == 0x1b && rx)
    {
        C(n == b->rx_n);
        memcpy(rx, b->rx, n);
    }
    return true;
}
static bool reset(void *ctx)
{
    (void)ctx;
    return true;
}
static bool ready(void *ctx)
{
    return ((bus *)ctx)->ready;
}
static void model_radio(st_radio *r, bus *b)
{
    *b = (bus){.ready = true};
    *r = (st_radio){.port = {b, command, reset, ready},
                    .phase = 13,
                    .config = {.tx_timeout_ms = 2200, .verified = true}};
    atomic_init(&r->irq,
                false); /* models an already-initialized radio, not a board configuration */
}
static void signal(st_radio *r, bus *b, uint16_t bits)
{
    b->irq |= bits;
    st_radio_irq_isr(r);
}
static void air(st_radio *from, bus *a, st_radio *to, bus *b)
{
    if (a->writes == a->seen)
        return;
    C(a->writes == a->seen + 1 && from->tx_active);
    a->seen = a->writes;
    memcpy(b->rx, a->tx, a->tx_n);
    b->rx_n = a->tx_n;
    signal(from, a, SX128X_IRQ_TX_DONE);
    signal(to, b, SX128X_IRQ_RX_DONE);
}
typedef struct
{
    uint64_t counter, peer;
    uint32_t epoch;
} nv;
static bool counter(void *ctx, uint64_t *v)
{
    nv *n = ctx;
    *v = ++n->counter;
    return true;
}
static bool epoch(void *ctx, uint64_t value, uint32_t *v)
{
    nv *n = ctx;
    if (value <= n->peer)
        return false;
    n->peer = value;
    *v = ++n->epoch;
    return true;
}
static st2_timing timing(void)
{
    return (st2_timing){10000, 2200, 2500, 100000, 4};
}
static void client_up(st2_client *c, nv *store)
{
    *store = (nv){0};
    st2_client_init(c, (st2_nv_port){store, NULL, counter}, timing(), (uint8_t[12]){2},
                    (uint8_t[12]){1});
    c->link = ST2_LINK_UP;
    c->epoch = 1;
    c->next_command = 1;
    c->have_status = true;
    c->flags = ST_IDLE;
    c->received_ms = 0;
}
static void client_completion(void)
{
    st_radio r;
    bus b;
    model_radio(&r, &b);
    st_link_runtime l;
    C(st_link_runtime_init(&l, &r));
    st2_client c;
    nv store;
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    st_link_client_step(&l, &c, 0, 7);
    C(r.tx_pending && c.attempts == 1 && c.waiting_tx_done && b.writes == 0);
    st_link_client_step(&l, &c, 1, 7);
    C(r.tx_active && c.waiting_tx_done && b.writes == 1);
    st_link_client_step(&l, &c, 1000, 7);
    C(c.attempts == 1 && c.last_send_ms == 0);
    signal(&r, &b, SX128X_IRQ_TX_DONE);
    st_link_client_step(&l, &c, 1001, 7);
    C(!c.waiting_tx_done && c.last_send_ms == 1001 && c.attempts == 1);
    st_link_client_step(&l, &c, 3507, 7);
    C(c.attempts == 1 && !r.tx_pending);
    st_link_client_step(&l, &c, 3508, 7);
    C(c.attempts == 2 && r.tx_pending && c.waiting_tx_done);
    st_link_client_step(&l, &c, 3509, 7);
    signal(&r, &b, SX128X_IRQ_TIMEOUT);
    st_link_client_step(&l, &c, 3600, 9);
    C(r.tx_timeouts == 1 && c.last_send_ms == 3600 && !c.waiting_tx_done);
    G("runtime client queues once / attempts only after RF acceptance / real TX_DONE and timeout "
      "counters anchor retry / no early ACK timer");
}
static void supersession(void)
{
    st_radio r;
    bus b;
    model_radio(&r, &b);
    st_link_runtime l;
    C(st_link_runtime_init(&l, &r));
    st2_client c;
    nv store;
    client_up(&c, &store);
    C(st2_client_cal_command(&c, NULL, 0));
    st_link_client_step(&l, &c, 0, 0);
    C(r.tx_pending && r.tx[0] == ST2_CAL_QUERY);
    uint8_t begin[14] = {0};
    C(st2_client_cal_command(&c, begin, 1));
    uint16_t id = c.request.id;
    st_link_client_step(&l, &c, 1, 0);
    C(b.writes == 0 && r.tx_pending && r.tx[0] == ST2_CAL_COMMAND && st_get16(r.tx + 5) == id);
    st_link_client_step(&l, &c, 2, 0);
    C(b.writes == 1 && b.tx[0] == ST2_CAL_COMMAND);
    /* Queued START must not get on air after explicit session renewal. */
    model_radio(&r, &b);
    C(st_link_runtime_init(&l, &r));
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    st_link_client_step(&l, &c, 0, 0);
    C(r.tx_pending);
    C(st2_client_connect(&c, 1));
    st_link_client_step(&l, &c, 1, 0);
    C(b.writes == 0 && r.tx_pending && r.tx[0] == ST2_HELLO && store.counter == 1);
    st_link_client_step(&l, &c, 2, 0);
    C(b.writes == 1 && b.tx[0] == ST2_HELLO);
    G("runtime superseded queued query and old-session START canceled before driver tick / only "
      "new explicit request transmitted");
}
static void deadlines_and_fault(void)
{
    st_radio r;
    bus b;
    model_radio(&r, &b);
    st_link_runtime l;
    C(st_link_runtime_init(&l, &r));
    st2_client c;
    nv store;
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    st_link_client_step(&l, &c, 0, 0);
    st_link_client_step(&l, &c, 100001, 0);
    C(!r.tx_pending && !r.tx_active && b.writes == 0 && c.transaction == ST_TX_TIMEOUT &&
      !c.pending);
    model_radio(&r, &b);
    C(st_link_runtime_init(&l, &r));
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    st_link_client_step(&l, &c, 0, 0);
    r.failed = true;
    st_link_client_step(&l, &c, 1, 0);
    C(l.fault && c.link == ST2_LINK_DOWN && !c.have_status && !c.pending &&
      c.transaction != ST_TX_COMPLETE);
    model_radio(&r, &b);
    C(st_link_runtime_init(&l, &r));
    st_link_client_step(&l, &c, 2, 0);
    C(!r.tx_pending && b.writes == 0 && c.link == ST2_LINK_DOWN && store.counter == 0);
    G("runtime total deadline removes unsent command / radio failure makes link unknown / explicit "
      "rebind never reconnects or replays START");
}
static void queued_api(void)
{
    st_radio r;
    bus b;
    model_radio(&r, &b);
    uint8_t out[35], bad[35];
    st2_packet p = {.type = ST2_START, .epoch = 1, .id = 1};
    size_t n = st2_encode(&p, out);
    C(st_radio_send(&r, out, n));
    memcpy(bad, out, n);
    bad[5] ^= 1;
    C(!st_radio_cancel_queued(&r, bad, n) && r.tx_pending);
    C(!st_radio_cancel_queued(&r, out, 50));
    C(st_radio_cancel_queued(&r, out, n) && !r.tx_pending);
    C(!st_radio_cancel_queued(&r, out, n));
    r.tx_active = true;
    C(!st_radio_cancel_queued(&r, out, n) && r.tx_active);
    st_link_runtime l;
    C(!st_link_runtime_init(&l, &r));
    r.tx_active = false;
    r.config.verified = false;
    C(!st_link_runtime_init(&l, &r));
    G("runtime exact-byte queued cancellation / bad length and mismatch refused / on-air "
      "transmission never recalled / busy-unverified bind rejected");
}
typedef struct
{
    uint8_t view[64];
    unsigned snapshots, requests;
} cal;
static bool cal_request(void *ctx, const uint8_t p[14])
{
    cal *v = ctx;
    v->requests++;
    v->view[0] = p[0] == 4 ? 6 : 1;
    return true;
}
static bool cal_snapshot(void *ctx, uint8_t p[64])
{
    cal *v = ctx;
    v->snapshots++;
    memcpy(p, v->view, 64);
    return true;
}
static void server_retention(void)
{
    st_logger *log = malloc(sizeof(*log));
    C(log != NULL);
    st_logger_init(log, (st_log_config){0}, (st_file_port){0});
    st2_server s;
    st2_server_init(&s, log, (st2_nv_port){0}, timing(), (uint8_t[12]){1}, (uint8_t[12]){2}, 0);
    s.offered = true;
    s.active = true;
    s.epoch = 1;
    cal v = {0};
    st2_server_attach_calibration(
        &s, (st2_cal_port){.ctx = &v, .request = cal_request, .snapshot = cal_snapshot});
    st_radio r;
    bus b;
    model_radio(&r, &b);
    st_link_runtime l;
    C(st_link_runtime_init(&l, &r));
    st2_packet p = {.type = ST2_CAL_QUERY, .epoch = 1, .id = 1};
    r.rx_length = (uint8_t)st2_encode(&p, r.rx);
    r.rx_ready = true;
    r.tx_active = true;
    r.tx_started_ms = 0;
    st_link_server_step(&l, &s, (st2_measurement){0}, 0);
    C(l.length == 26 && v.snapshots == 1 && b.writes == 0);
    uint8_t frozen[35];
    memcpy(frozen, l.packet, l.length);
    v.view[0] = 8;
    for (unsigned i = 1; i < 4; i++)
        st_link_server_step(&l, &s, (st2_measurement){0}, i * 1000u);
    C(l.length == 26 && !memcmp(frozen, l.packet, 26) && v.snapshots == 1);
    signal(&r, &b, SX128X_IRQ_TX_DONE);
    st_link_server_step(&l, &s, (st2_measurement){0}, 4000);
    C(!l.length && r.tx_pending && !memcmp(r.tx, frozen, 26));
    unsigned seen = 0;
    uint8_t mask = 0;
    for (uint32_t now = 5; now < 100 && seen < 4; now++)
    {
        st_link_server_step(&l, &s, (st2_measurement){0}, (uint64_t)now * 1000);
        if (b.writes > b.seen)
        {
            b.seen = b.writes;
            st2_packet frame;
            C(st2_decode(&frame, b.tx, b.tx_n));
            C(frame.type == ST2_CAL_REPLY && frame.fragment == (uint8_t)(0x30u + seen));
            C(frame.data[0] == 0);
            mask |= (uint8_t)(1u << (frame.fragment & 15u));
            seen++;
            signal(&r, &b, SX128X_IRQ_TX_DONE);
        }
    }
    C(seen == 4 && mask == 15 && v.snapshots == 1 && v.requests == 0);
    free(log);
    G("runtime server holds frozen reply while RF busy / no snapshot overwrite / all four "
      "calibration fragments transmitted in order with bounded slots");
}
static void duplex(void)
{
    st_radio rc, rs;
    bus bc, bs;
    model_radio(&rc, &bc);
    model_radio(&rs, &bs);
    st_link_runtime lc, ls;
    C(st_link_runtime_init(&lc, &rc) && st_link_runtime_init(&ls, &rs));
    nv nc = {0}, ns = {.epoch = 100};
    st2_client c;
    st2_client_init(&c, (st2_nv_port){&nc, NULL, counter}, timing(), (uint8_t[12]){2},
                    (uint8_t[12]){1});
    st_logger *log = malloc(sizeof(*log));
    C(log != NULL);
    st_logger_init(log, (st_log_config){0}, (st_file_port){0});
    st2_server s;
    st2_server_init(&s, log, (st2_nv_port){&ns, epoch, NULL}, timing(), (uint8_t[12]){1},
                    (uint8_t[12]){2}, 0);
    cal v = {0};
    st2_server_attach_calibration(
        &s, (st2_cal_port){.ctx = &v, .request = cal_request, .snapshot = cal_snapshot});
    C(st2_client_connect(&c, 0));
    uint32_t now = 0;
    for (; now < 1000 && c.link != ST2_LINK_UP; now += 10)
    {
        st_link_client_step(&lc, &c, now, 0);
        st_link_server_step(&ls, &s, (st2_measurement){0}, (uint64_t)now * 1000);
        air(&rc, &bc, &rs, &bs);
        air(&rs, &bs, &rc, &bc);
    }
    C(c.link == ST2_LINK_UP && s.active && nc.counter == 1 && ns.peer == 1 && c.epoch == 101);
    uint8_t save[14] = {4};
    C(st2_client_cal_command(&c, save, now));
    for (unsigned i = 0; i < 200 && c.pending; i++, now += 10)
    {
        st_link_client_step(&lc, &c, now, 0);
        st_link_server_step(&ls, &s, (st2_measurement){0}, (uint64_t)now * 1000);
        air(&rc, &bc, &rs, &bs);
        air(&rs, &bs, &rc, &bc);
    }
    C(!c.pending && c.cal_have_view && c.cal_view[0] == 6 && v.requests == 1 &&
      c.transaction == ST_TX_COMPLETE);
    C(!lc.fault && !ls.fault);
    free(log);
    G("runtime duplex official radio-state-machine model / durable-callback handshake / actual "
      "epoch mapping / four-fragment cal result / no automatic START");
}
static void stop_ordering(void)
{
    st_radio r;
    bus b;
    st_link_runtime l;
    st2_client c;
    nv store;
    model_radio(&r, &b);
    C(st_link_runtime_init(&l, &r));
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    st_link_client_step(&l, &c, 0, 0);
    C(r.tx_pending && c.waiting_tx_done);
    C(st2_client_command(&c, ST2_STOP, 1));
    st_link_client_step(&l, &c, 1, 0);
    C(r.tx_active && b.tx[0] == ST2_START && c.stop == ST2_STOP_QUEUED);
    C(c.request.type == ST2_START && c.request.id == 1);
    signal(&r, &b, SX128X_IRQ_TX_DONE);
    st_link_client_step(&l, &c, 2, 0);
    C(r.tx_pending && r.tx[0] == ST2_STOP && c.request.id == 2);
    C(c.stop == ST2_STOP_ACTIVE && c.waiting_tx_done && c.attempts == 1);
    st2_packet late = {.type = ST2_ACK,
                       .epoch = 1,
                       .id = 1,
                       .flags = (uint8_t)(ST2_COMPLETED | (ST_CAPTURING << 3))};
    r.rx_length = (uint8_t)st2_encode(&late, r.rx);
    r.rx_ready = true;
    st_link_client_step(&l, &c, 3, 0);
    C(r.tx_active && b.writes == 2 && b.tx[0] == ST2_STOP && c.pending);
    C(c.transaction == ST_TX_WAIT_ACK && c.stop == ST2_STOP_ACTIVE);
    signal(&r, &b, SX128X_IRQ_TIMEOUT);
    st_link_client_step(&l, &c, 4, 0);
    st_link_client_step(&l, &c, 2504, 0);
    C(r.tx_pending && r.tx[0] == ST2_STOP && st_get16(r.tx + 5) == 2);
    C(c.attempts == 2 && c.stop == ST2_STOP_ACTIVE);
    r.failed = true;
    st_link_client_step(&l, &c, 2505, 0);
    C(!c.pending && c.stop == ST2_STOP_UNCONFIRMED && c.link == ST2_LINK_DOWN);
    model_radio(&r, &b);
    C(st_link_runtime_init(&l, &r));
    st_link_client_step(&l, &c, 2506, 0);
    C(!r.tx_pending && b.writes == 0 && c.stop == ST2_STOP_UNCONFIRMED);
    /* STOP before START has even reached the RF owner: transmit only its fence. */
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    C(st2_client_command(&c, ST2_STOP, 0));
    st_link_client_step(&l, &c, 0, 0);
    st_link_client_step(&l, &c, 1, 0);
    C(b.writes == 1 && b.tx[0] == ST2_STOP && st_get16(b.tx + 5) == 2);
    /* Fault while the original START is still queued also retains uncertainty. */
    model_radio(&r, &b);
    C(st_link_runtime_init(&l, &r));
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    st_link_client_step(&l, &c, 0, 0);
    C(st2_client_command(&c, ST2_STOP, 1));
    r.failed = true;
    st_link_client_step(&l, &c, 1, 0);
    C(c.stop == ST2_STOP_UNCONFIRMED && !c.pending);
    /* A late terminal START ACK can arrive while a retransmission is queued
     * behind BUSY. Canceling that unsent copy must release its TX wait token. */
    model_radio(&r, &b);
    C(st_link_runtime_init(&l, &r));
    client_up(&c, &store);
    C(st2_client_command(&c, ST2_START, 0));
    st_link_client_step(&l, &c, 0, 0);
    b.ready = false;
    C(st2_client_command(&c, ST2_STOP, 1));
    r.rx_length = (uint8_t)st2_encode(&late, r.rx);
    r.rx_ready = true;
    st_link_client_step(&l, &c, 1, 0);
    C(!c.pending && c.stop == ST2_STOP_QUEUED && r.tx_pending);
    st_link_client_step(&l, &c, 2, 0);
    C(r.tx_pending && r.tx[0] == ST2_STOP && c.request.id == 2);
    C(c.pending && c.stop == ST2_STOP_ACTIVE);
    b.ready = true;
    st_link_client_step(&l, &c, 3, 0);
    C(b.writes == 1 && b.tx[0] == ST2_STOP);
    G("runtime queued/on-air START then newer STOP / TX_DONE ordering / late START ACK ignored / "
      "STOP RF timeout retries same ID / RF fault retains uncertainty / no automatic replay");
}
static void Ignition_TestRuntime(void)
{
    st_radio rc, rs; bus bc, bs;
    model_radio(&rc, &bc); model_radio(&rs, &bs);
    st_link_runtime lc, ls;
    C(st_link_runtime_init(&lc, &rc) && st_link_runtime_init(&ls, &rs));
    st2_client c; nv nc; client_up(&c, &nc);
    st_logger *log = malloc(sizeof *log); C(log != NULL);
    st_logger_init(log, (st_log_config){0}, (st_file_port){0});
    st2_server s;
    st2_server_init(&s, log, (st2_nv_port){0}, timing(), (uint8_t[12]){1},
                    (uint8_t[12]){2}, 0);
    s.epoch = 1; s.active = s.offered = true;
    const uint8_t key[16] = {1};
    IgnitionClient client; IgnitionServer server;
    IgnitionClient_Init(&client, s.uid, s.peer, 2500, 100000, 4);
    IgnitionServer_Init(&server, s.uid, s.peer);
    C(IgnitionClient_SetKey(&client, key) == IGNITION_KEY_OK);
    C(IgnitionServer_SetKey(&server, key) == IGNITION_KEY_OK);
    C(IgnitionClient_SetSession(&client, 1, true) == IGNITION_SESSION_OK);
    c.ignition = &client; s.ignition = &server;
    uint32_t now = 0;
    for (unsigned operation = 0; operation < 3; operation++)
    {
        IgnitionRequestResult result = operation == 0 ? IgnitionClient_Query(&client, true, 0, now)
            : operation == 1 ? IgnitionClient_RequestArm(&client, 1, 1, now)
                             : IgnitionClient_RequestDisarm(&client, now);
        C(result == IGNITION_REQUEST_OK);
        for (unsigned step = 0; step < 100 && client.pending; step++, now += 10)
        {
            st_link_client_step(&lc, &c, now, 0);
            st_link_server_step(&ls, &s, (st2_measurement){0}, (uint64_t)now * 1000u);
            air(&rc, &bc, &rs, &bs); air(&rs, &bs, &rc, &bc);
        }
        C(!client.pending && client.have_reply && client.state == IGNITION_DISABLED);
        C(client.reason == IGNITION_REASON_UNBOUND && !client.arm_id);
        if (operation == 0) C(client.have_details && !client.profile_id);
        if (operation == 1) C(client.result == IGNITION_REJECTED);
        if (operation == 2) C(client.result == IGNITION_FAILED);
        C(!c.pending && c.next_command == 1 && log->state == ST_IDLE);
    }
    C(!lc.fault && !ls.fault);
    /* A query waiting behind BUSY is replaced, never transmitted as a stale copy. */
    C(IgnitionClient_Query(&client, true, 0, now) == IGNITION_REQUEST_OK);
    bc.ready = false;
    st_link_client_step(&lc, &c, now++, 0);
    C(rc.tx_pending && rc.tx[7] == IGNITION_QUERY);
    C(IgnitionClient_RequestDisarm(&client, now) == IGNITION_REQUEST_OK);
    st_link_client_step(&lc, &c, now++, 0);
    C(rc.tx_pending && rc.tx[7] == IGNITION_DISARM && client.waiting_tx);
    bc.ready = true;
    st_link_client_step(&lc, &c, now++, 0);
    C(rc.tx_active && bc.tx[7] == IGNITION_DISARM);
    free(log);
    G("runtime authenticated 33/35B duplex query/detail/ARM/DISARM, explicit UNBOUND, no acquisition dispatch, queued request supersession");
}
static void Ignition_TestExpiredQueuedFire(void)
{
    st_radio radio; bus model; model_radio(&radio, &model);
    st_link_runtime runtime; C(st_link_runtime_init(&runtime, &radio));
    st2_client legacy; nv store; client_up(&legacy, &store);
    IgnitionClient client;
    IgnitionClient_Init(&client, legacy.peer, legacy.uid, 2500, 100000, 4);
    C(IgnitionClient_SetKey(&client, (uint8_t[16]){1}) == IGNITION_KEY_OK);
    C(IgnitionClient_SetSession(&client, 1, true) == IGNITION_SESSION_OK);
    legacy.ignition = &client;
    C(IgnitionClient_RequestArm(&client, 1, 1, 0) == IGNITION_REQUEST_OK);
    st_link_client_step(&runtime, &legacy, 0, 0);
    st_link_client_step(&runtime, &legacy, 1, 0);
    C(radio.tx_active && model.writes == 1);
    signal(&radio, &model, SX128X_IRQ_TX_DONE);
    st_link_client_step(&runtime, &legacy, 2, 0);
    /* An authenticated fixture grant tests only the client transport. The real
     * acquisition endpoint can never generate this grant in the UNBOUND build. */
    IgnitionPacket grant = {.type=IGNITION_REPLY,.epoch=1,.id=1,
                            .data={IGNITION_COMMAND_REPLY,IGNITION_ARM_GRANTED,IGNITION_ARMED}};
    st_put32(grant.data + 4, 9); st_put16(grant.data + 8, 50);
    size_t length = 0;
    C(Ignition_Encode(&client.auth, &grant, radio.rx, &length) == IGNITION_CODEC_OK);
    radio.rx_length = (uint8_t)length; radio.rx_ready = true;
    st_link_client_step(&runtime, &legacy, 3, 0);
    C(client.have_grant && client.grant_deadline_ms == 50);
    C(IgnitionClient_RequestFire(&client, 4) == IGNITION_REQUEST_OK);
    model.ready = false;
    st_link_client_step(&runtime, &legacy, 4, 0);
    C(radio.tx_pending && radio.tx[7] == IGNITION_FIRE && model.writes == 1);
    st_link_client_step(&runtime, &legacy, 51, 0);
    C(!radio.tx_pending && !client.pending && model.writes == 1);
    model.ready = true;
    st_link_client_step(&runtime, &legacy, 52, 0);
    C(!radio.tx_active && model.writes == 1);
    G("runtime expired queued FIRE removed before radio tick, conservative grant deadline, no late or automatic transmission");
}
typedef struct { uint8_t slots[2][ST_NV_BYTES]; } AutoTest_Media;
static bool AutoTest_Read(void *ctx, unsigned slot, uint8_t out[ST_NV_BYTES])
{ AutoTest_Media *media=ctx;memcpy(out,media->slots[slot],ST_NV_BYTES);return true; }
static bool AutoTest_Write(void *ctx, unsigned slot, const uint8_t in[ST_NV_BYTES])
{ AutoTest_Media *media=ctx;memcpy(media->slots[slot],in,ST_NV_BYTES);return true; }
static bool AutoTest_Idle(void *ctx) { (void)ctx;return true; }
static void AutoTest_Runtime(void)
{
    AutoTest_Media ma,mc;memset(&ma,255,sizeof ma);memset(&mc,255,sizeof mc);
    st_pairing pa,pc;
    C(st_pairing_open(&pa,(st_nv_storage){&ma,AutoTest_Read,AutoTest_Write,true},
                      (uint8_t[12]){1},ST_NV_ACQUISITION)==ST_NV_EMPTY);
    C(st_pairing_open(&pc,(st_nv_storage){&mc,AutoTest_Read,AutoTest_Write,true},
                      (uint8_t[12]){2},ST_NV_CONTROLLER)==ST_NV_EMPTY);
    const uint8_t key[16]={1};
    AutoLink aa,ac;
    AutoLink_Init(&aa,&pa,key,0x5354,NULL,AutoTest_Idle);
    AutoLink_Init(&ac,&pc,key,0x5354,NULL,AutoTest_Idle);
    st_radio ra,rc;bus ba,bc;model_radio(&ra,&ba);model_radio(&rc,&bc);
    st_link_runtime la,lc;C(st_link_runtime_init(&la,&ra)&&st_link_runtime_init(&lc,&rc));
    la.auto_link=&aa;lc.auto_link=&ac;
    st2_server server={0};st2_client client={0};
    for(uint32_t now=0;now<200;now+=10)
    {
        st_link_client_step(&lc,&client,now,0);
        st_link_server_step(&la,&server,(st2_measurement){0},(uint64_t)now*1000u);
        air(&rc,&bc,&ra,&ba);air(&ra,&ba,&rc,&bc);
    }
    C(pa.store.status==ST_NV_VALID&&pc.store.status==ST_NV_VALID);
    C(AutoLink_TakeChanged(&aa)&&AutoLink_TakeChanged(&ac));
    C(ba.writes==1&&bc.writes==1&&ba.tx[0]==AUTO_LINK_ADVERTISE&&bc.tx[0]==AUTO_LINK_DISCOVER);
    st2_packet decoded;uint8_t wire[35];
    C(st2_decode(&decoded,ba.tx,ba.tx_n)&&st2_encode(&decoded,wire)==ba.tx_n&&!memcmp(wire,ba.tx,ba.tx_n));
    C(st2_decode(&decoded,bc.tx,bc.tx_n)&&st2_encode(&decoded,wire)==bc.tx_n&&!memcmp(wire,bc.tx,bc.tx_n));
    st_logger *logger=calloc(1,sizeof *logger);C(logger);
    st_logger_init(logger,(st_log_config){0},(st_file_port){0});
    C(st_nv_server_init(&server,&pa.store,logger,timing()));
    C(st_nv_client_init(&client,&pc.store,timing()));
    C(st2_client_connect(&client,200));
    for(uint32_t now=200;now<1000&&client.link!=ST2_LINK_UP;now+=10)
    {
        st_link_client_step(&lc,&client,now,0);
        st_link_server_step(&la,&server,(st2_measurement){0},(uint64_t)now*1000u);
        air(&rc,&bc,&ra,&ba);air(&ra,&ba,&rc,&bc);
    }
    C(client.link==ST2_LINK_UP&&server.active&&client.epoch==server.epoch);
    C(logger->state==ST_IDLE&&!client.pending);
    free(logger);
    G("automatic discovery through real radio encoder/RX gate, pristine UID provisioning, then durable HELLO/COMMIT; never starts capture");
}
int main(void)
{
    stop_ordering();
    client_completion();
    supersession();
    deadlines_and_fault();
    queued_api();
    server_retention();
    duplex();
    Ignition_TestRuntime();
    Ignition_TestExpiredQueuedFire();
    AutoTest_Runtime();
    printf("PASS targeted runtime: %u groups, %u assertions; sizeof(runtime)=%zu\n", groups, checks,
           sizeof(st_link_runtime));
    return 0;
}
