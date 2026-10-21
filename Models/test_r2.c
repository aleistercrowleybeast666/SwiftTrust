/* R8 replaces obsolete R2 wire/UI assumptions; low-level suites remain separate. */
#include "st_ui.h"
#include "st_session_zero.h"
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
            fprintf(stderr, "R8 line%d: %s\n", __LINE__, #x);                                      \
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
    uint64_t counter, last;
    uint32_t epoch;
    unsigned writes;
} Nv;
static bool Counter(void *ctx, uint64_t *out)
{
    Nv *v = ctx;
    if (v->counter == UINT64_MAX)
        return false;
    *out = ++v->counter;
    v->writes++;
    return true;
}
static bool Epoch(void *ctx, uint64_t n, uint32_t *out)
{
    Nv *v = ctx;
    if (n <= v->last || v->epoch == UINT32_MAX)
        return false;
    v->last = n;
    *out = ++v->epoch;
    v->writes++;
    return true;
}
static const uint8_t acq[12] = {1, 2, 3}, ctrl[12] = {4, 5, 6};
static st2_timing Timing(void)
{
    return (st2_timing){2000, 150, 500, 120000, 4};
}
static st2_measurement Measured(void)
{
    return (st2_measurement){ST2_TF_READY, true, true, false, 4100};
}
static size_t Pack(st2_packet p, uint8_t *out)
{
    size_t n = st2_encode(&p, out);
    C(n);
    return n;
}
static void Connect(st2_client *c, st2_server *s, uint32_t now)
{
    uint8_t x[ST2_MAX], y[ST2_MAX];
    size_t n, m;
    C(st2_client_connect(c, now));
    for (unsigned i = 0; i < 6 && c->link != ST2_LINK_UP; i++)
    {
        C(st2_client_tx_due(c, now, x, &n));
        st2_client_tx_accepted(c);
        st2_client_tx_done(c, now, 0);
        m = st2_server_receive(s, x, n, Measured(), (uint64_t)now * 1000, y);
        C(m);
        C(st2_client_receive(c, y, m, now));
    }
    C(c->link == ST2_LINK_UP);
    C(Link_QueryTask(c, now));
    n = st2_encode(&c->request, x);
    m = st2_server_receive(s, x, n, Measured(), (uint64_t)now * 1000, y);
    C(m);
    C(st2_client_receive(c, y, m, now) && !c->need_recovery);
}
static void Codec(void)
{
    const uint8_t types[] = {
        ST2_HELLO,      ST2_HELLO_REPLY, ST2_COMMIT,         ST2_STATUS,        ST2_START,
        ST2_STOP,       ST2_ACK,         ST2_DETAILS_QUERY,  ST2_DETAILS_REPLY, ST2_CAL_COMMAND,
        ST2_CAL_QUERY,  ST2_CAL_REPLY,   ST2_SENSOR_COMMAND, ST2_SENSOR_QUERY,  ST2_SENSOR_REPLY,
        ST2_TASK_QUERY, ST2_TASK_REPLY,  ST2_STATUS_ACK};
    const size_t sizes[] = {50, 43, 26, 24, 19, 19, 21, 18, 35, 32, 18, 35, 20, 18, 30, 18, 30, 18};
    uint8_t x[ST2_MAX], y[ST2_MAX];
    unsigned kinds = 0;
    for (unsigned i = 0; i < sizeof types; i++)
    {
        st2_packet p = {.type = types[i],
                        .epoch = 0x12345678,
                        .id = UINT32_C(70000),
                        .counter = 1,
                        .mv = 4100,
                        .count = 16,
                        .fragment = 0x30,
                        .mode = ST_TASK_CAPTURE,
                        .section = 1};
        memcpy(p.uid, acq, 12);
        memcpy(p.peer, ctrl, 12);
        if (p.type == ST2_DETAILS_REPLY)
            p.fragment = 0x10;
        if (p.type == ST2_HELLO || p.type == ST2_HELLO_REPLY || p.type == ST2_COMMIT)
            p.id = 0;
        size_t n = Pack(p, x);
        C(n == sizes[i] && x[0] == 0xD8 && x[1] == 8);
        kinds |= 1u << x[2];
        st2_packet q;
        C(st2_decode(&q, x, n) && q.id == p.id && q.epoch == p.epoch && q.type == p.type);
        for (size_t k = 0; k < n; k++)
        {
            memcpy(y, x, n);
            y[k] ^= 1;
            C(!st2_decode(&q, y, n));
        }
        for (size_t k = 0; k < n; k++)
            C(!st2_decode(&q, x, k));
        memcpy(y, x, n);
        y[1] = 7;
        st_put16(y + n - 2, st_crc16(y, n - 2));
        C(!st2_decode(&q, y, n));
    }
    C(kinds == 0xFE);
    for (unsigned old = 1; old <= 16; old++)
    {
        memset(x, 0, sizeof x);
        x[0] = (uint8_t)old;
        st_put16(x + 33, st_crc16(x, 33));
        st2_packet p;
        C(!st2_decode(&p, x, 35));
    }
    C(!st2_encode(&(st2_packet){.type = ST2_START, .epoch = 1, .id = 0}, x));
    G("R8 seven logical types / exact lengths / 32-bit IDs / every byte corruption and truncation "
      "/ explicit R7 rejection");
}
static void Sessions(void)
{
    Nv cn = {0}, sn = {0};
    st_logger l;
    st_logger_init(&l, (st_log_config){0}, (st_file_port){0});
    st2_client c;
    st2_server s;
    st2_client_init(&c, (st2_nv_port){&cn, NULL, Counter}, Timing(), ctrl, acq);
    st2_server_init(&s, &l, (st2_nv_port){&sn, Epoch, NULL}, Timing(), acq, ctrl, 0);
    Connect(&c, &s, 0);
    C(sn.writes == 1 && cn.writes == 1 && c.task.state == ST_TASK_IDLE);
    uint32_t session = s.epoch;
    unsigned writes = sn.writes + cn.writes;
    /* Recovery has no logger side effects and writes no Flash even while capturing. */
    l.state = ST_CAPTURING;
    l.generation = 123;
    s.task = (StTask){ST_TASK_CAPTURE, ST_TASK_RUNNING, 77, 0};
    c.pending = false;
    Connect(&c, &s, 8000);
    C(c.resumed && s.epoch == session && l.state == ST_CAPTURING && l.generation == 123);
    c.have_status = false;
    c.link = ST2_LINK_COMMIT;
    uint8_t stale[64];
    size_t stale_n =
        Pack((st2_packet){.type = ST2_STATUS, .epoch = c.epoch, .id = c.status_sequence}, stale);
    C(!st2_client_receive(&c, stale, stale_n, 8001) && !c.have_status);
    c.link = ST2_LINK_UP;
    c.have_status = true;
    C(sn.writes + cn.writes == writes && c.task.state == ST_TASK_RUNNING && c.task.start_id == 77);
    /* A rebooted controller with counter zero can resume the acquisition's session. */
    st2_client reboot;
    st2_client_init(&reboot, (st2_nv_port){&cn, NULL, Counter}, Timing(), ctrl, acq);
    Connect(&reboot, &s, 9000);
    C(reboot.resumed && reboot.task.state == ST_TASK_RUNNING);
    uint8_t x[64], y[64];
    st2_packet hello = {.type = ST2_HELLO, .counter = 999, .pair_id = ST_PROJECT_PAIR_ID + 1};
    memcpy(hello.uid, ctrl, 12);
    memcpy(hello.peer, acq, 12);
    size_t n = Pack(hello, x);
    C(!st2_server_receive(&s, x, n, Measured(), 0, y) && l.state == ST_CAPTURING);
    hello.pair_id = ST_PROJECT_PAIR_ID;
    hello.uid[0] ^= 1;
    n = Pack(hello, x);
    C(!st2_server_receive(&s, x, n, Measured(), 0, y));
    /* Acquisition reboot creates a different session, keeps UNKNOWN and never STARTs. */
    l.state = ST_IDLE;
    st2_server_init(&s, &l, (st2_nv_port){&sn, Epoch, NULL}, Timing(), acq, ctrl, sn.last);
    reboot.pending = false;
    Connect(&reboot, &s, 10000);
    C(s.epoch != session && reboot.task.state == ST_TASK_UNKNOWN && l.generation == 123 &&
      !reboot.pending);
    G("R8 Pair/UID isolation / resumed session after loss and controller reboot / no NV writes or "
      "file restart / acquisition reboot UNKNOWN");
}
static void Transactions(void)
{
    st_logger l;
    st_logger_init(&l, (st_log_config){0}, (st_file_port){0});
    st2_server s;
    st2_server_init(&s, &l, (st2_nv_port){0}, Timing(), acq, ctrl, 1);
    s.offered = s.active = true;
    s.epoch = 5;
    uint8_t x[64], y[64], saved[64];
    st2_packet p = {.type = ST2_STOP, .epoch = 5, .id = 65536};
    size_t n = Pack(p, x);
    size_t m = st2_server_receive(&s, x, n, Measured(), 0, y);
    C(m == 21);
    memcpy(saved, y, m);
    C(st2_server_receive(&s, x, n, Measured(), 100, y) == m && !memcmp(saved, y, m));
    p.type = ST2_START;
    n = Pack(p, x);
    m = st2_server_receive(&s, x, n, Measured(), 100, y);
    st2_packet reply;
    C(st2_decode(&reply, y, m) && (reply.flags & 7u) == ST2_CONFLICT && l.state == ST_IDLE);
    p.id--;
    n = Pack(p, x);
    m = st2_server_receive(&s, x, n, Measured(), 100, y);
    C(st2_decode(&reply, y, m) && (reply.flags & 7u) == ST2_OLD);
    st2_client c;
    st2_client_init(&c, (st2_nv_port){0}, Timing(), ctrl, acq);
    c.link = ST2_LINK_UP;
    c.have_status = true;
    c.epoch = 5;
    c.next_command = 70000;
    c.task.mode = ST_TASK_CAPTURE;
    C(st2_client_command(&c, ST2_START, 0));
    C(c.task.state == ST_TASK_STARTING);
    p = (st2_packet){.type = ST2_STATUS,
                     .epoch = 5,
                     .id = 1,
                     .flags = (uint8_t)(ST_CAPTURING | (ST2_TF_OPEN << 3)),
                     .task_state = ST_TASK_RUNNING,
                     .mode = ST_TASK_CAPTURE};
    n = Pack(p, x);
    C(st2_client_receive(&c, x, n, 10) && c.pending && c.task.state == ST_TASK_STARTING);
    C(!st2_client_receive(&c, x, n, 20) && c.received_ms == 10);
    p = (st2_packet){.type = ST2_ACK,
                     .epoch = 5,
                     .id = 70000,
                     .flags = (uint8_t)(ST2_COMPLETED | (ST_CAPTURING << 3) | (ST2_TF_OPEN << 6))};
    n = Pack(p, x);
    C(st2_client_receive(&c, x, n, 20) && !c.pending && c.task.state == ST_TASK_RUNNING);
    C(!st2_client_command(&c, ST2_START, 20));
    C(st2_client_command(&c, ST2_STOP, 20));
    C(!st2_client_receive(&c, x, n, 30) && c.pending && c.task.state == ST_TASK_STOPPING);
    G("R8 duplicate result cache / conflicting and old transactions / IDs cross65535 / STATUS "
      "cannot complete START / late reply cannot replace STOP");
}
static void Details(void)
{
    st_logger logger;
    st_logger_init(&logger, (st_log_config){0}, (st_file_port){0});
    logger.last_sequence = 777;
    logger.have_sample = true;
    logger.last_time_us = 100000;
    st2_server server;
    st2_server_init(&server, &logger, (st2_nv_port){0}, Timing(), acq, ctrl, 0);
    server.active = server.offered = true;
    server.epoch = 5;
    st2_client client;
    st2_client_init(&client, (st2_nv_port){0}, Timing(), ctrl, acq);
    client.link = ST2_LINK_UP;
    client.epoch = 5;
    client.have_status = true;
    client.next_command = 70000;
    C(Link_QueryDetails(&client, 0) == ST_LINK_REQUEST_OK);
    uint8_t request[64], first[64], second[64], repeat[64];
    size_t n = st2_encode(&client.request, request);
    size_t a = st2_server_receive(&server, request, n, Measured(), 1000000, first);
    logger.last_sequence = 999;
    size_t b = st2_server_poll(&server, Measured(), 1001000, second);
    C(a == 35 && b == 26);
    C(st2_client_receive(&client, second, b, 1) && client.pending);
    C(st2_client_receive(&client, first, a, 1) && !client.pending && client.details.complete);
    C(st_get32(client.details.data + 4) == 777 && client.details_received_ms == 1);
    C(st2_server_receive(&server, request, n, Measured(), 9000000, repeat) == a &&
      !memcmp(repeat, first, a));
    C(Link_QueryDetails(&client, 1) == ST_LINK_REQUEST_OK);
    C(!st2_client_receive(&client, first, a, 2) && client.pending && !client.details.complete);
    client.pending = false;
    client.need_recovery = true;
    C(Link_QueryTask(&client, 2) == ST_LINK_REQUEST_OK);
    n = Pack(
        (st2_packet){
            .type = ST2_ACK, .epoch = client.epoch, .id = client.request.id, .flags = ST2_OLD},
        repeat);
    C(st2_client_receive(&client, repeat, n, 2) && client.link == ST2_LINK_DOWN && !client.pending);
    G("R8 on-demand diagnostic query uses common ID / frozen two-part DATA / reorder / duplicate / "
      "late view cannot replace new query");
}
static void Heartbeat(void)
{
    st_logger logger;
    st_logger_init(&logger, (st_log_config){0}, (st_file_port){0});
    st2_server server;
    st2_server_init(&server, &logger, (st2_nv_port){0}, Timing(), acq, ctrl, 0);
    server.active = server.offered = true;
    server.epoch = 1;
    uint8_t x[64], y[64];
    C(st2_server_poll(&server, Measured(), 0, y) == 24);
    C(!st2_server_poll(&server, Measured(), 1999000, y));
    C(st2_server_poll(&server, Measured(), 2000000, y) == 24);
    st2_packet ack = {.type = ST2_STATUS_ACK, .epoch = 1, .id = 1};
    size_t n = Pack(ack, x);
    C(!st2_server_receive(&server, x, n, Measured(), 2000000, y) && !server.have_peer_status);
    ack.id = 2;
    n = Pack(ack, x);
    C(!st2_server_receive(&server, x, n, Measured(), 2000000, y) && server.have_peer_status);
    C(server.peer_received_ms == 2000);
    C(!st2_server_receive(&server, x, n, Measured(), 6000000, y) &&
      server.peer_received_ms == 2000);
    C(Link_GetHealth(true, server.peer_received_ms, 6000, false) == ST_LINK_SLOW);
    G("R8 exact2s heartbeat / independent STATUS sequence / bidirectional receipt ACK / old ACK "
      "does not refresh LED age");
}
static void Menus(void)
{
    st2_client c = {.link = ST2_LINK_UP,
                    .have_status = true,
                    .next_command = 1,
                    .epoch = 1,
                    .timing = {2000, 150, 500, 120000, 4}};
    st_ui ui;
    st_ui_model m;
    st_ui_init(&ui);
    C(ui.selection == 0);
    for (unsigned mode = 0; mode < 3; mode++)
    {
        st_ui_init(&ui);
        ui.selection = mode;
        C(!st_ui2_handle(&ui, ST_UI_PRESS, &c, 0) && ui.selection == 0);
        C(Ui_GetItemCount(&ui) == (mode == 2 ? 2u : 4u));
        st_ui2_render(&ui, &c, (st_voltage_view){0}, 0, &m);
        C(m.inverse && m.inverse_row == 0);
        C(!strcmp(m.rows[0], Ui_Text("返回", "BACK")));
        if (mode == 2)
        {
            C(!m.rows[2][0] && !m.rows[3][0]);
            for (unsigned i = 0; i < 9; i++)
            {
                (void)st_ui2_handle(&ui, ST_UI_NEXT, &c, 0);
                C(ui.selection < 2);
            }
        }
        ui.selection = 1;
        (void)st_ui2_handle(&ui, ST_UI_PRESS, &c, 0);
        C(ui.page == ST_UI_CONFIRM && ui.selection == 0 && !c.pending);
        (void)st_ui2_handle(&ui, ST_UI_PRESS, &c, 0);
        C(ui.selection == 0 && !c.pending);
    }
    st_ui_init(&ui);
    ui.selection = 3;
    (void)st_ui2_handle(&ui, ST_UI_PRESS, &c, 0);
    for (unsigned i = 0; i < 9; i++)
    {
        st_ui2_render(&ui, &c, (st_voltage_view){0}, 0, &m);
        C(m.inverse_row < 4);
        (void)st_ui2_handle(&ui, ST_UI_NEXT, &c, 0);
    }
    ui.page = ST_UI_STATUS;
    ui.top = 2;
    ui.selection = 2;
    c.flags = 0;
    c.mv = 4100;
    st_ui2_render(&ui, &c, (st_voltage_view){0}, 0, &m);
    C(strstr(m.rows[0], Ui_Text("未知", "UNKNOWN")));
    c.flags = 32;
    c.mv = 0;
    st_ui2_render(&ui, &c, (st_voltage_view){0}, 0, &m);
    C(strstr(m.rows[0], "0.00V"));
    c.task = (StTask){ST_TASK_CAPTURE, ST_TASK_RUNNING, 1, 0};
    ui.page = ST_UI_CAPTURE;
    ui.mode = ST_TASK_CAPTURE;
    ui.selection = 0;
    (void)st_ui2_handle(&ui, ST_UI_PRESS, &c, 0);
    C(ui.page == ST_UI_HOME && c.task.state == ST_TASK_RUNNING && !c.pending);
    for (unsigned state = 0; state <= ST_TASK_UNKNOWN; state++)
        for (unsigned mode = 1; mode <= 3; mode++)
        {
            ui.page = mode == 1 ? ST_UI_CAPTURE : mode == 2 ? ST_UI_COMBINED : ST_UI_IGNITION_ONLY;
            ui.mode = (StTaskMode)mode;
            ui.selection = 1;
            ui.top = 0;
            c.task.mode = (StTaskMode)mode;
            c.task.state = (StTaskState)state;
            st_ui2_render(&ui, &c, (st_voltage_view){0}, 0, &m);
            for (unsigned row = 0; row < 4; row++)
            {
                unsigned width;
                C(Ui_ValidateText(m.rows[row], &width) == UI_TEXT_OK && width <= 128);
            }
        }
    unsigned width;
    C(Ui_ValidateText("\xC0\xAF", &width) == UI_TEXT_INVALID);
    C(Ui_ValidateText("1234567890123456789012", &width) == UI_TEXT_TOO_WIDE);
    st_button b = {0};
    C(Button_Update(&b, false, 0) == ST_BUTTON_NONE);
    C(Button_Update(&b, true, 1) == ST_BUTTON_NONE);
    C(Button_Update(&b, true, 31) == ST_BUTTON_NONE);
    C(Button_Update(&b, true, 831) == ST_BUTTON_LONG);
    C(Button_Update(&b, true, 900) == ST_BUTTON_NONE);
    C(Button_Update(&b, false, 901) == ST_BUTTON_NONE);
    C(Button_Update(&b, false, 931) == ST_BUTTON_NONE);
    C(Button_Update(&b, true, 1000) == ST_BUTTON_NONE);
    C(Button_Update(&b, true, 1030) == ST_BUTTON_NONE);
    C(Button_Update(&b, false, 1100) == ST_BUTTON_NONE);
    C(Button_Update(&b, false, 1130) == ST_BUTTON_SHORT);
    C(Link_GetHealth(true, 0, 3999, false) == ST_LINK_OK);
    C(Link_GetHealth(true, 0, 4000, false) == ST_LINK_SLOW);
    C(Link_GetHealth(true, 0, 7000, false) == ST_LINK_LOST);
    C(Link_GetHealth(true, 0, 0, true) == ST_LINK_DRIVER_FAULT);
    C(Link_GetLed(ST_LINK_OK, 1) && !Link_GetLed(ST_LINK_WAIT, 1));
    G("R8 four fixed rows / default BACK / safe confirmation / output no STOP or blank cursor / "
      "scrolling / task labels / UTF8 / long-short button / 4s-7s LED");
}
unsigned st_test_r2(unsigned *out)
{
    Codec();
    Sessions();
    Transactions();
    Details();
    Heartbeat();
    Menus();
    *out += groups;
    return checks;
}
