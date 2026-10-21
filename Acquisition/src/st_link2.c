#include "st_link2.h"
#include <string.h>
bool st2_timing_valid(st2_timing t)
{
    return t.heartbeat_ms >= 1000 && t.heartbeat_ms <= 30000 && t.max_airtime_ms >= 1 &&
           t.max_airtime_ms <= 10000 && t.ack_rto_ms > t.max_airtime_ms && t.ack_rto_ms <= 60000 &&
           t.operation_ms >= t.ack_rto_ms && t.operation_ms <= 600000 && t.attempts >= 1 &&
           t.attempts <= 10;
}
void st2_server_init(st2_server *s, st_logger *l, st2_nv_port nv, st2_timing t,
                     const uint8_t uid[12], const uint8_t peer[12], uint64_t counter)
{
    *s = (st2_server){
        .logger = l, .nv = nv, .timing = t, .last_counter = counter, .pair_id = ST_PROJECT_PAIR_ID};
    memcpy(s->uid, uid, 12);
    memcpy(s->peer, peer, 12);
}
void st2_server_attach_calibration(st2_server *s, st2_cal_port p)
{
    s->calibration = p;
}
static void cal_reset(st2_server *s)
{
    if (s->calibration.reset)
        s->calibration.reset(s->calibration.ctx);
    s->cal_cached = false;
    s->cal_owned = false;
}
static size_t cal_reply(st2_server *s, uint8_t *out)
{
    if (!s->cal_cached || s->cal_next >= 4)
        return 0;
    unsigned i = s->cal_next++;
    st2_packet p = {.type = ST2_CAL_REPLY,
                    .epoch = s->epoch,
                    .id = s->cal_id,
                    .fragment = (uint8_t)(0x30u | i),
                    .count = 16};
    memcpy(p.data, s->cal_view + i * 16, 16);
    return st2_encode(&p, out);
}
static uint8_t status_flags(st2_server *s, st2_measurement m)
{
    if (m.quality_problem || st_logger_error_count(s->logger) ||
        atomic_load(&s->logger->ring.dropped))
        s->quality_sticky = true;
    return (uint8_t)((unsigned)s->logger->state | ((unsigned)(m.tf & 3u) << 3) |
                     ((m.voltage_fresh && m.mv != 65535) ? 32u : 0u) | (m.adc_fresh ? 64u : 0u) |
                     (s->quality_sticky ? 128u : 0u));
}
static uint8_t ack_flags(st2_server *s, st2_measurement m, uint8_t result)
{
    return (uint8_t)(result | ((unsigned)s->logger->state << 3) | ((unsigned)(m.tf & 3u) << 6));
}
/* Logger progresses independently in its storage task. Upgrade the cached
 * acceptance exactly once on an observed terminal state; retries only read it. */
static void finish_command(st2_server *s, st2_measurement m)
{
    if (s->start_deferred)
    {
        bool ready =
            s->calibration.prepare_start && s->calibration.prepare_start(s->calibration.ctx);
        if (ready)
        {
            s->start_deferred = false;
            if (!st_logger_start(s->logger, s->now_us))
                s->task.state = ST_TASK_FAILED;
        }
        else if ((s->calibration.start_failed && s->calibration.start_failed(s->calibration.ctx)) ||
                 !s->calibration.start_waiting || !s->calibration.start_waiting(s->calibration.ctx))
        {
            s->start_deferred = false;
            s->task.state = ST_TASK_FAILED;
        }
        if (s->start_deferred)
            return;
    }
    if (Task_IsRecordingMode(s->task.mode) && s->task.state == ST_TASK_RUNNING &&
        (s->logger->state == ST_FAULT || s->logger->state == ST_IDLE))
        s->task.state = ST_TASK_FAILED;
    if (s->task.mode == ST_TASK_OUTPUT && s->ignition && s->task.state == ST_TASK_RUNNING)
    {
        if (s->ignition->state == IGNITION_LOCKOUT)
            Task_CompleteOutput(&s->task, true);
        else if (s->ignition->state == IGNITION_FAULT)
            Task_CompleteOutput(&s->task, false);
    }
    if (s->task.mode == ST_TASK_OUTPUT && s->ignition && s->task.state == ST_TASK_STARTING &&
        s->ignition->state == IGNITION_SAFE)
        s->task.state = ST_TASK_STOPPED;
    if (!s->have_command || s->last_result != ST2_ACCEPTED)
        return;
    uint8_t result = s->logger->state == ST_FAULT ? ST2_FAILED : ST2_ACCEPTED;
    if (s->last_type == ST2_SENSOR_COMMAND)
    {
        uint8_t view[12] = {0};
        if (s->calibration.sensor_snapshot &&
            s->calibration.sensor_snapshot(s->calibration.ctx, view))
            result = view[0] == 1 ? ST2_ACCEPTED : view[0] == 2 ? ST2_COMPLETED : ST2_FAILED;
    }
    /* A source fault or explicit reconnect can cancel START before capture.
     * Once the logger drains to IDLE, fail that START instead of leaving it
     * ACCEPTED forever and rejecting every subsequent explicit command. */
    if (s->last_type == ST2_START && s->logger->state == ST_IDLE)
        result = ST2_FAILED;
    if ((s->last_type == ST2_START && s->logger->state == ST_CAPTURING) ||
        (s->last_type == ST2_STOP && s->logger->state == ST_IDLE))
        result = ST2_COMPLETED;
    if (result != ST2_ACCEPTED)
    {
        s->last_result = result;
        s->terminal_pending = true;
        s->last_flags = ack_flags(s, m, result);
        if (s->last_type == ST2_START)
            Task_CompleteStart(&s->task, s->last_command, result == ST2_COMPLETED);
        if (s->last_type == ST2_STOP)
            Task_CompleteStop(&s->task, s->last_command, result == ST2_COMPLETED);
        for (unsigned i = 0; i < 8; i++)
            if (s->history[i].id == s->last_command)
            {
                st2_packet cached = {.type = ST2_ACK,
                                     .pair_id = s->pair_id,
                                     .epoch = s->epoch,
                                     .id = s->last_command,
                                     .flags = s->last_flags,
                                     .task_state = (uint8_t)s->task.state,
                                     .mode = (uint8_t)s->task.mode};
                s->history[i].reply_n = (uint8_t)st2_encode(&cached, s->history[i].reply);
            }
    }
}
static size_t send_ack(st2_server *s, uint32_t id, uint8_t flags, uint8_t *out)
{
    st2_packet p = {.type = ST2_ACK,
                    .epoch = s->epoch,
                    .id = id,
                    .flags = flags,
                    .pair_id = s->pair_id,
                    .task_state = (uint8_t)s->task.state,
                    .mode = (uint8_t)s->task.mode};
    return st2_encode(&p, out);
}
static size_t send_status(st2_server *s, st2_measurement m, uint32_t now, uint8_t *out)
{
    if (s->status_sequence == UINT32_MAX)
    {
        s->active = false;
        return 0;
    } /* renew, never wrap */
    uint8_t f = status_flags(s, m);
    st2_packet p = {.type = ST2_STATUS,
                    .epoch = s->epoch,
                    .id = ++s->status_sequence,
                    .flags = f,
                    .mv = m.mv,
                    .pair_id = s->pair_id,
                    .task_state = (uint8_t)s->task.state,
                    .mode = (uint8_t)s->task.mode,
                    .errors = (uint8_t)(s->quality_sticky ? 1u : 0u)};
    s->have_status = true;
    s->previous_flags = f;
    s->previous_mv = m.mv;
    s->last_status_ms = now;
    return st2_encode(&p, out);
}
static void detail_snapshot(st2_server *s, uint64_t now)
{
    st_logger *l = s->logger;
    uint8_t *p = s->snapshot;
    st_put32(p, l->config.clock_verified ? l->config.rate_millihz : 0);
    st_put32(p + 4, l->last_sequence);
    st_put32(p + 8, l->durable_sequence);
    st_put32(p + 12, atomic_load(&l->ring.dropped));
    st_put32(p + 16, st_logger_error_count(l));
    uint64_t age =
        (!l->have_sample || now < l->last_time_us) ? UINT64_MAX : (now - l->last_time_us) / 1000;
    st_put16(p + 20, age >= 65535 ? 65535 : (uint16_t)age);
    p[22] = (uint8_t)((l->have_sample ? 1u : 0u) | (l->have_durable ? 2u : 0u) |
                      (l->config.clock_verified ? 4u : 0u) | (l->config.simulated ? 8u : 0u));
}
static size_t detail_reply(st2_server *s, uint8_t *out)
{
    if (s->detail_next >= 2)
        return 0;
    unsigned index = s->detail_next++;
    st2_packet p = {.type = ST2_DETAILS_REPLY,
                    .epoch = s->epoch,
                    .id = s->detail_id,
                    .section = s->detail_section,
                    .fragment = (uint8_t)(0x10u | index),
                    .count = (uint8_t)(index ? 7 : 16)};
    memcpy(p.data, s->snapshot + index * 16, p.count);
    return st2_encode(&p, out);
}
static size_t receive_operation(st2_server *s, const uint8_t *in, size_t n, st2_measurement m,
                                uint64_t now, uint8_t out[ST2_MAX])
{
    st2_packet p;
    if (!s->logger || !st2_timing_valid(s->timing) || m.tf > 3 || !st2_decode(&p, in, n))
        return 0;
    if (p.type == ST2_HELLO)
    {
        if (memcmp(p.uid, s->peer, 12) || memcmp(p.peer, s->uid, 12))
            return 0;
        /* A live acquisition session is resumed without Flash IO, logger STOP,
         * calibration reset, sequence reset or a fresh execution authorization. */
        if (s->active)
        {
            st2_packet resume = {.type = ST2_HELLO_REPLY,
                                 .epoch = s->epoch,
                                 .counter = p.counter,
                                 .pair_id = s->pair_id,
                                 .flags = 2};
            memcpy(resume.uid, s->uid, 12);
            st_put32(resume.data, s->highest_id);
            return st2_encode(&resume, out);
        }
        if (p.counter <= s->last_counter && !(s->offered && p.counter == s->last_counter))
        {
            st2_packet offer = {
                .type = ST2_HELLO_REPLY, .counter = p.counter, .pair_id = s->pair_id, .flags = 1};
            memcpy(offer.uid, s->uid, 12);
            return st2_encode(&offer, out);
        }
        if (!(s->offered && p.counter == s->last_counter))
        {
            uint32_t epoch = 0;
            if (p.counter <= s->last_counter)
                return 0;
            if (s->logger->state != ST_IDLE || !s->nv.reserve_epoch ||
                !s->nv.reserve_epoch(s->nv.ctx, p.counter, &epoch) || !epoch)
                return 0;
            if (s->last_counter)
                s->task.state = ST_TASK_UNKNOWN;
            s->last_counter = p.counter;
            s->epoch = epoch;
            s->offered = true;
            s->active = false;
            if (s->ignition)
                (void)IgnitionServer_SetSession(s->ignition, 0, false);
            s->have_command = false;
            s->terminal_pending = false;
            s->have_status = false;
            s->status_sequence = 0;
            s->detail_cached = false;
            s->highest_id = 0;
            s->history_next = 0;
            memset(s->history, 0, sizeof s->history);
        }
        st2_packet reply = {.type = ST2_HELLO_REPLY,
                            .epoch = s->epoch,
                            .counter = p.counter,
                            .pair_id = s->pair_id};
        st_put32(reply.data, s->highest_id);
        memcpy(reply.uid, s->uid, 12);
        return st2_encode(&reply, out);
    }
    if (!s->offered || p.epoch != s->epoch || p.pair_id != s->pair_id)
        return 0;
    if (p.type == ST2_COMMIT)
    {
        if (!s->active && p.counter != s->last_counter)
            return 0;
        s->active = true;
        if (s->ignition)
            (void)IgnitionServer_SetSession(s->ignition, s->epoch, true);
        return send_status(s, m, (uint32_t)(now / 1000), out);
    }
    if (!s->active)
        return 0;
    if (p.type == ST2_IGN_REQUEST)
    {
        if (!s->ignition ||
            IgnitionServer_SetSession(s->ignition, s->epoch, true) != IGNITION_SESSION_OK)
            return 0;
        return IgnitionServer_Receive(s->ignition, in, n, out);
    }
    finish_command(s, m);
    if (p.type == ST2_TASK_QUERY)
    {
        st2_packet snapshot = {
            .type = ST2_TASK_REPLY, .epoch = s->epoch, .pair_id = s->pair_id, .id = p.id};
        snapshot.data[0] = (uint8_t)s->task.state;
        snapshot.data[1] = (uint8_t)s->task.mode;
        snapshot.data[2] = s->ignition ? s->ignition->state : IGNITION_DISABLED;
        snapshot.data[3] = s->output_started ? 1 : 0;
        st_put32(snapshot.data + 4, s->task.start_id);
        st_put32(snapshot.data + 8, s->task.stop_id);
        return st2_encode(&snapshot, out);
    }
    if (p.type == ST2_SENSOR_QUERY)
    {
        st2_packet view = {
            .type = ST2_SENSOR_REPLY, .epoch = s->epoch, .pair_id = s->pair_id, .id = p.id};
        if (!s->calibration.sensor_snapshot ||
            !s->calibration.sensor_snapshot(s->calibration.ctx, view.data))
            return 0;
        return st2_encode(&view, out);
    }
    if (p.type == ST2_SENSOR_COMMAND)
    {
        if (p.data[0] < 7 || p.data[0] > 8 || p.data[1] > 1 || s->logger->state != ST_IDLE ||
            IgnitionServer_IsBusy(s->ignition) || !s->calibration.request)
            return send_ack(s, p.id, ack_flags(s, m, ST2_STATE_REJECT), out);
        bool accepted = s->calibration.request(s->calibration.ctx, p.data);
        s->last_command = p.id;
        s->last_type = p.type;
        s->have_command = true;
        s->last_result = accepted ? (p.data[0] == 7 ? ST2_ACCEPTED : ST2_COMPLETED) : ST2_FAILED;
        s->last_flags = ack_flags(s, m, s->last_result);
        return send_ack(s, p.id, s->last_flags, out);
    }
    if (p.type == ST2_DETAILS_QUERY)
    {
        if (p.section != 1 || (s->detail_cached && p.id < s->detail_id))
            return 0;
        if (!s->detail_cached || p.id != s->detail_id)
        {
            s->detail_id = p.id;
            s->detail_section = p.section;
            s->detail_cached = true;
            detail_snapshot(s, now);
        }
        s->detail_next = 0;
        return detail_reply(s, out);
    }
    if (p.type == ST2_CAL_COMMAND || p.type == ST2_CAL_QUERY)
    {
        if (IgnitionServer_IsBusy(s->ignition))
            return send_ack(s, p.id, ack_flags(s, m, ST2_STATE_REJECT), out);
        if (s->have_command && p.id < s->last_command)
            return send_ack(s, p.id, ack_flags(s, m, ST2_OLD), out);
        if (s->have_command && p.id == s->last_command)
        {
            if (!s->cal_cached || p.type != s->last_type ||
                (p.type == ST2_CAL_COMMAND && memcmp(p.data, s->cal_request, 14)))
                return send_ack(s, p.id, ack_flags(s, m, ST2_CONFLICT), out);
            s->cal_next = 0;
            return cal_reply(s, out);
        }
        if ((s->have_command && s->last_result == ST2_ACCEPTED) || !s->calibration.snapshot ||
            (p.type == ST2_CAL_COMMAND && !s->calibration.request))
            return send_ack(s, p.id, ack_flags(s, m, ST2_STATE_REJECT), out);
        bool ok = true;
        if (p.type == ST2_CAL_COMMAND)
            ok = s->calibration.request(s->calibration.ctx, p.data);
        if (!s->calibration.snapshot(s->calibration.ctx, s->cal_view))
            return send_ack(s, p.id, ack_flags(s, m, ST2_FAILED), out);
        if (!ok)
            s->cal_view[3] = 1; /* rejected; never a durable-save acknowledgement */
        s->cal_last_ms = (uint32_t)(now / 1000);
        s->cal_owned = true;
        s->have_command = true;
        s->last_command = p.id;
        s->cal_id = p.id;
        s->last_type = p.type;
        s->last_result = ST2_COMPLETED;
        s->last_flags = ack_flags(s, m, ST2_COMPLETED);
        memcpy(s->cal_request, p.data, 14);
        s->cal_cached = true;
        s->cal_next = 0;
        return cal_reply(s, out);
    }
    if (p.type != ST2_START && p.type != ST2_STOP)
        return 0;
    uint8_t result;
    if (s->have_command && p.id < s->last_command)
        result = ST2_OLD;
    else if (s->have_command && p.id == s->last_command)
    {
        if (p.type != s->last_type)
            result = ST2_CONFLICT;
        else
            return send_ack(s, p.id, s->last_flags, out);
    }
    /* A newer STOP may cancel STARTING. Once cached, its greater ID fences
     * every late/retried START, even if STOP arrived before START itself.
     * No other command can supersede an accepted operation. */
    else if (s->have_command && s->last_result == ST2_ACCEPTED &&
             !(p.type == ST2_STOP && s->last_type == ST2_START))
        result = ST2_STATE_REJECT;
    else
    {
        bool accepted = false;
        if (p.type == ST2_START)
        {
            StTaskMode mode = p.mode ? (StTaskMode)p.mode : ST_TASK_CAPTURE;
            if (!IgnitionServer_IsBusy(s->ignition) && Task_IsRecordingMode(mode) &&
                (!s->calibration.locked || !s->calibration.locked(s->calibration.ctx)) &&
                Task_Begin(&s->task, mode, p.id) == ST_TASK_OK)
            {
                if (s->calibration.begin_start)
                    s->calibration.begin_start(s->calibration.ctx);
                bool ready = !s->calibration.prepare_start ||
                             s->calibration.prepare_start(s->calibration.ctx);
                s->start_deferred = !ready && s->calibration.start_waiting &&
                                    s->calibration.start_waiting(s->calibration.ctx);
                accepted = s->start_deferred || (ready && st_logger_start(s->logger, now));
                if (!accepted)
                    s->task.state = ST_TASK_FAILED;
                else
                    s->output_started = false;
            }
        }
        else
        {
            s->start_deferred = false;
            if (s->have_command && s->last_type == ST2_START && s->last_result == ST2_ACCEPTED)
                for (unsigned i = 0; i < 8; i++)
                    if (s->history[i].id == s->last_command)
                    {
                        st2_packet canceled = {.type = ST2_ACK,
                                               .epoch = s->epoch,
                                               .pair_id = s->pair_id,
                                               .id = s->last_command,
                                               .flags = ack_flags(s, m, ST2_FAILED)};
                        s->history[i].reply_n = (uint8_t)st2_encode(&canceled, s->history[i].reply);
                    }
            if (s->calibration.reset)
                s->calibration.reset(s->calibration.ctx);
            if (s->ignition && s->ignition->output_bound)
            {
                (void)s->ignition->output.stop(s->ignition->output.ctx);
                IgnitionServer_Service(s->ignition);
                s->ignition->arm_id = 0;
                s->ignition->state = IGNITION_SAFE;
                s->ignition->deadline_ms = 0;
            }
            accepted = st_logger_stop(s->logger);
            Task_RequestStop(&s->task, p.id);
            if (!accepted)
                s->task.state = ST_TASK_FAILED;
        }
        result = accepted ? ST2_ACCEPTED : ST2_STATE_REJECT;
        s->have_command = true;
        s->last_command = p.id;
        s->last_type = p.type;
        s->cal_cached = false;
        s->last_result = result;
        s->last_flags = ack_flags(s, m, result);
        finish_command(s, m);
        return send_ack(s, p.id, s->last_flags, out);
    }
    return send_ack(s, p.id, ack_flags(s, m, result), out);
}
size_t st2_server_receive(st2_server *s, const uint8_t *in, size_t n, st2_measurement m,
                          uint64_t now, uint8_t out[ST2_MAX])
{
    st2_packet request;
    if (!s || !st2_decode(&request, in, n) || request.pair_id != s->pair_id)
        return 0;
    s->now_us = now;
    bool operation = request.type != ST2_HELLO && request.type != ST2_COMMIT;
    if (!operation)
        return receive_operation(s, in, n, m, now, out);
    if (!s->active || request.epoch != s->epoch)
        return 0;
    finish_command(s, m);
    if (request.type == ST2_STATUS_ACK)
    {
        if (request.id == s->status_sequence && request.id > s->peer_status_sequence)
        {
            s->peer_status_sequence = request.id;
            s->peer_received_ms = (uint32_t)(now / 1000);
            s->have_peer_status = true;
        }
        return 0;
    }
    if (request.type == ST2_IGN_REQUEST)
    {
        IgnitionPacket verified;
        if (!s->ignition || Ignition_Decode(&s->ignition->auth, IGNITION_REQUEST, in, n,
                                            &verified) != IGNITION_CODEC_OK)
            return 0;
    }
    for (unsigned i = 0; i < 8; i++)
        if (s->history[i].id == request.id)
        {
            StTransactionCache *cached = &s->history[i];
            if (cached->request_n != n || memcmp(cached->request, in, n))
                return send_ack(s, request.id, ack_flags(s, m, ST2_CONFLICT), out);
            if (request.type == ST2_IGN_REQUEST && request.data[0] != IGNITION_QUERY)
            {
                size_t updated = IgnitionServer_Receive(s->ignition, in, n, out);
                if (updated)
                {
                    memcpy(cached->reply, out, updated);
                    cached->reply_n = (uint8_t)updated;
                }
            }
            if (request.type == ST2_CAL_COMMAND || request.type == ST2_CAL_QUERY)
            {
                memcpy(s->cal_view, cached->view, 64);
                s->cal_id = request.id;
                s->cal_next = 1;
                s->cal_cached = true;
            }
            if (request.type == ST2_DETAILS_QUERY)
            {
                memcpy(s->snapshot, cached->view, 23);
                s->detail_id = request.id;
                s->detail_next = 1;
                s->detail_cached = true;
            }
            if (request.type == ST2_IGN_REQUEST && request.data[0] == IGNITION_QUERY &&
                request.data[1])
            {
                memcpy(s->ignition->query_reply[1], cached->view, IGNITION_REPLY_BYTES);
                s->ignition->query_count = 2;
                s->ignition->query_next = 1;
            }
            memcpy(out, cached->reply, cached->reply_n);
            return cached->reply_n;
        }
    if (request.id <= s->highest_id)
        return send_ack(s, request.id, ack_flags(s, m, ST2_OLD), out);
    if (request.type == ST2_IGN_REQUEST && request.data[0] != IGNITION_QUERY)
        s->ignition->last_command = s->highest_id;
    size_t size = receive_operation(s, in, n, m, now, out);
    if (!size)
        return 0;
    s->highest_id = request.id;
    StTransactionCache *cached = &s->history[s->history_next++ % 8u];
    *cached =
        (StTransactionCache){.id = request.id, .request_n = (uint8_t)n, .reply_n = (uint8_t)size};
    memcpy(cached->request, in, n);
    memcpy(cached->reply, out, size);
    if (request.type == ST2_CAL_COMMAND || request.type == ST2_CAL_QUERY)
        memcpy(cached->view, s->cal_view, 64);
    if (request.type == ST2_DETAILS_QUERY)
        memcpy(cached->view, s->snapshot, 23);
    if (request.type == ST2_IGN_REQUEST)
    {
        if (request.data[0] == IGNITION_QUERY && request.data[1])
            memcpy(cached->view, s->ignition->query_reply[1], IGNITION_REPLY_BYTES);
        IgnitionPacket reply;
        if (Ignition_Decode(&s->ignition->auth, IGNITION_REPLY, out, size, &reply) ==
            IGNITION_CODEC_OK)
        {
            if (request.data[0] == IGNITION_ARM && request.data[1] == 1 &&
                reply.data[1] == IGNITION_ARM_GRANTED)
                s->task = (StTask){ST_TASK_OUTPUT, ST_TASK_STARTING, request.id, 0};
            if (request.data[0] == IGNITION_FIRE && request.data[1] == 1 &&
                reply.data[1] == IGNITION_FIRE_ACCEPTED)
            {
                s->task.state = ST_TASK_RUNNING;
                s->output_started = true;
            }
            if (request.data[0] == IGNITION_DISARM && s->task.mode == ST_TASK_OUTPUT &&
                reply.data[1] == IGNITION_DISARMED && s->task.state != ST_TASK_COMPLETED)
                s->task.state = ST_TASK_STOPPED;
        }
    }
    return size;
}
size_t st2_server_poll(st2_server *s, st2_measurement m, uint64_t now, uint8_t out[ST2_MAX])
{
    s->now_us = now;
    if (!s->active || m.tf > 3)
        return 0;
    finish_command(s, m);
    if (s->cal_owned &&
        (uint32_t)((uint32_t)(now / 1000) - s->cal_last_ms) > s->timing.operation_ms)
    {
        if (s->calibration.locked && s->calibration.locked(s->calibration.ctx))
            cal_reset(s); /* lost controller releases a draft, never a committed result */
        else
            s->cal_owned = false;
    }
    if (s->terminal_pending)
    {
        s->terminal_pending = false;
        return send_ack(s, s->last_command, s->last_flags, out);
    }
    if (s->ignition)
    {
        size_t ignition_length = IgnitionServer_Poll(s->ignition, out);
        if (ignition_length)
            return ignition_length;
    }
    if (s->cal_cached && s->cal_next < 4)
        return cal_reply(s, out);
    if (s->detail_cached && s->detail_next < 2)
        return detail_reply(s, out);
    uint8_t f = status_flags(s, m);
    uint32_t ms = (uint32_t)(now / 1000);
    /* Voltage magnitude alone waits for heartbeat; freshness/tier/state events send immediately. */
    if (!s->have_status || f != s->previous_flags ||
        (uint32_t)(ms - s->last_status_ms) >= s->timing.heartbeat_ms)
        return send_status(s, m, ms, out);
    return 0;
}
void st2_client_init(st2_client *c, st2_nv_port nv, st2_timing t, const uint8_t uid[12],
                     const uint8_t peer[12])
{
    *c = (st2_client){.nv = nv, .timing = t, .pair_id = ST_PROJECT_PAIR_ID};
    memcpy(c->uid, uid, 12);
    memcpy(c->peer, peer, 12);
}
bool st2_client_connect(st2_client *c, uint32_t now)
{
    uint64_t counter = c->counter;
    if (!st2_timing_valid(c->timing) || c->pending)
        return false;
    if (c->pending)
        c->transaction = ST_TX_REBOOT;
    c->counter = counter;
    /* Keep task and unresolved STOP intent across a transport reconnect. */
    c->link = ST2_LINK_HELLO;
    c->have_status = false;
    c->cal_have_view = false;
    c->cal_seen = 0;
    c->details.complete = false;
    c->sensor_have_view = false;
    c->pending = false;
    c->request = (st2_packet){
        .type = ST2_HELLO, .counter = counter, .epoch = c->epoch, .pair_id = c->pair_id};
    memcpy(c->request.uid, c->uid, 12);
    memcpy(c->request.peer, c->peer, 12);
    c->attempts = 0;
    c->have_sent = false;
    c->waiting_tx_done = false;
    c->started_ms = now;
    return true;
}
bool st2_client_fresh(const st2_client *c, uint32_t now)
{
    return c->link == ST2_LINK_UP && c->have_status &&
           (uint32_t)(now - c->received_ms) <= ST_LINK_LOST_MS - 1u;
}
bool st2_client_receive(st2_client *c, const uint8_t *in, size_t n, uint32_t now)
{
    st2_packet p;
    if (!st2_decode(&p, in, n) || p.pair_id != c->pair_id)
        return false;
    if (p.type == ST2_HELLO_REPLY && c->link == ST2_LINK_HELLO && p.counter == c->counter &&
        !memcmp(p.uid, c->peer, 12))
    {
        if (!p.epoch && p.flags == 1)
        {
            uint64_t next = 0;
            if (!c->nv.reserve_counter || !c->nv.reserve_counter(c->nv.ctx, &next) ||
                next <= c->counter)
            {
                c->link = ST2_LINK_DOWN;
                return false;
            }
            c->counter = next;
            c->request.counter = next;
            c->have_sent = false;
            c->waiting_tx_done = false;
            c->attempts = 0;
            c->started_ms = now;
            return true;
        }
        if (!p.epoch)
            return false;
        if (c->epoch && p.epoch < c->epoch)
            return false;
        c->resumed = p.flags == 2;
        if (c->epoch != p.epoch)
        {
            c->status_sequence = 0;
            c->task.state = ST_TASK_UNKNOWN;
        }
        uint32_t floor = st_get32(p.data);
        if (floor == UINT32_MAX)
        {
            c->link = ST2_LINK_DOWN;
            return false;
        }
        if (c->next_command <= floor)
            c->next_command = floor + 1u;
        c->need_recovery = true;
        c->epoch = p.epoch;
        c->request = (st2_packet){
            .type = ST2_COMMIT, .epoch = p.epoch, .counter = c->counter, .pair_id = c->pair_id};
        c->link = ST2_LINK_COMMIT;
        c->attempts = 0;
        c->have_sent = false;
        c->waiting_tx_done = false;
        c->started_ms = now;
        return true;
    }
    if (!c->epoch || p.epoch != c->epoch)
        return false;
    if (p.type == ST2_STATUS && (c->link == ST2_LINK_COMMIT || c->link == ST2_LINK_UP))
    {
        if (p.id <= c->status_sequence)
            return false;
        bool first = c->link == ST2_LINK_COMMIT;
        c->link = ST2_LINK_UP;
        c->have_status = true;
        c->received_ms = now;
        c->status_sequence = p.id;
        c->flags = p.flags;
        c->mv = p.mv;
        c->remote_task = p.task_state;
        c->remote_mode = p.mode;
        c->errors = p.errors;
        if (p.task_state == ST_TASK_FAILED && c->task.state == ST_TASK_RUNNING)
            c->task.state = ST_TASK_FAILED;
        if (first)
        {
            if (!c->next_command)
                c->next_command = 1;
            c->waiting_tx_done = false;
        }
        return true; /* status NEVER completes a transaction */
    }
    if (p.type == ST2_CAL_REPLY && c->link == ST2_LINK_UP && c->pending && p.id == c->request.id &&
        (c->request.type == ST2_CAL_COMMAND || c->request.type == ST2_CAL_QUERY))
    {
        unsigned i = p.fragment & 15u;
        uint8_t bit = (uint8_t)(1u << i);
        if (c->cal_seen & bit)
            return !memcmp(c->cal_view + i * 16, p.data, 16);
        memcpy(c->cal_view + i * 16, p.data, 16);
        c->cal_seen |= bit;
        if (c->cal_seen == 15)
        {
            c->cal_have_view = true;
            c->cal_received_ms = now;
            c->pending = false;
            c->transaction = ST_TX_COMPLETE;
        }
        return true;
    }
    if (p.type == ST2_TASK_REPLY && c->pending && c->request.type == ST2_TASK_QUERY &&
        p.id == c->request.id)
    {
        if (p.data[0] > ST_TASK_UNKNOWN || p.data[1] > ST_TASK_OUTPUT)
            return false;
        c->task = (StTask){p.data[1] ? (StTaskMode)p.data[1] : ST_TASK_CAPTURE,
                           (StTaskState)p.data[0], st_get32(p.data + 4), st_get32(p.data + 8)};
        c->output_started = p.data[3] != 0;
        c->pending = false;
        c->transaction = ST_TX_COMPLETE;
        c->need_recovery = false;
        return true;
    }
    if (p.type == ST2_DETAILS_REPLY && c->pending && c->request.type == ST2_DETAILS_QUERY &&
        p.id == c->request.id)
    {
        if (!st2_details_receive(&c->details, in, n))
            return false;
        if (c->details.complete)
        {
            c->pending = false;
            c->transaction = ST_TX_COMPLETE;
            c->details_received_ms = now;
        }
        return true;
    }
    if (p.type == ST2_SENSOR_REPLY && c->pending && c->request.type == ST2_SENSOR_QUERY &&
        p.id == c->request.id)
    {
        memcpy(c->sensor_view, p.data, 12);
        c->sensor_have_view = true;
        c->pending = false;
        c->transaction = ST_TX_COMPLETE;
        return true;
    }
    if (p.type == ST2_ACK && c->link == ST2_LINK_UP && c->pending && p.id == c->request.id)
    {
        uint8_t result = p.flags & 7u;
        if (c->request.type == ST2_TASK_QUERY && (result == ST2_OLD || result == ST2_CONFLICT))
        {
            c->pending = false;
            c->link = ST2_LINK_DOWN;
            c->transaction = ST_TX_REJECTED;
            return true; /* Re-handshake for a fresh high-water mark, never count through old IDs.
                          */
        }
        if (c->request.type == ST2_STOP && result == ST2_COMPLETED &&
            (((p.flags >> 3) & 7u) != ST_IDLE || ((p.flags >> 6) & 3u) == ST2_TF_OPEN))
            return false;
        if (c->request.type == ST2_START && result == ST2_COMPLETED &&
            (((p.flags >> 3) & 7u) != ST_CAPTURING || ((p.flags >> 6) & 3u) != ST2_TF_OPEN))
            return false;
        if (result == ST2_ACCEPTED)
        {
            c->transaction = ST_TX_WAIT_STATE;
            c->attempts = 0;
        }
        else
        {
            c->pending = false;
            c->transaction = result == ST2_COMPLETED ? ST_TX_COMPLETE : ST_TX_REJECTED;
            if (c->request.type == ST2_STOP)
            {
                c->stop = result == ST2_COMPLETED ? ST2_STOP_NONE : ST2_STOP_UNCONFIRMED;
                Task_CompleteStop(&c->task, p.id, result == ST2_COMPLETED);
            }
            if (c->request.type == ST2_START)
                Task_CompleteStart(&c->task, p.id, result == ST2_COMPLETED);
        }
        return true; /* ACK does not refresh telemetry age */
    }
    return false;
}
static void begin_command(st2_client *c, uint8_t type, uint32_t now)
{
    c->request = (st2_packet){.type = type,
                              .epoch = c->epoch,
                              .id = c->next_command,
                              .pair_id = c->pair_id,
                              .mode = (uint8_t)c->task.mode};
    c->next_command = c->next_command == ST_TRANSACTION_LIMIT ? 0 : c->next_command + 1u;
    c->pending = true;
    c->transaction = ST_TX_WAIT_ACK;
    c->attempts = 0;
    c->have_sent = false;
    c->waiting_tx_done = false;
    c->started_ms = now;
    if (type == ST2_STOP)
        c->stop = ST2_STOP_ACTIVE;
}
bool st2_client_command(st2_client *c, uint8_t type, uint32_t now)
{
    if (type == ST2_START && c->need_recovery)
        return false;
    if (type != ST2_START && type != ST2_STOP)
        return false;
    if (type == ST2_STOP &&
        (c->stop == ST2_STOP_QUEUED || (c->pending && c->request.type == ST2_STOP)))
        return true; /* No new ID, retry-budget reset or deadline extension. */
    if (type == ST2_STOP && c->pending && c->request.type == ST2_START)
    {
        if (!c->next_command)
            return false;
        /* Intent may be recorded while telemetry is stale. Its transmission
         * still waits for fresh state and expires at the bounded deadline. */
        c->stop = ST2_STOP_QUEUED;
        c->stop_requested_ms = now;
        Task_RequestStop(&c->task, c->next_command);
        return true;
    }
    if (!st2_client_fresh(c, now))
    {
        if (type != ST2_STOP || !c->next_command)
            return false;
        c->stop = ST2_STOP_QUEUED;
        c->stop_requested_ms = now;
        Task_RequestStop(&c->task, c->next_command);
        return true;
    }
    if (type == ST2_STOP && c->stop == ST2_STOP_UNCONFIRMED && !c->pending &&
        c->request.type == ST2_STOP)
    {
        /* Explicit recovery retries the identical command, including after a
         * lost terminal ACK. Never let old telemetry trigger a new START. */
        c->pending = true;
        c->stop = ST2_STOP_ACTIVE;
        c->transaction = ST_TX_WAIT_ACK;
        c->attempts = 0;
        c->have_sent = false;
        c->waiting_tx_done = false;
        c->started_ms = now;
        return true;
    }
    if (!c->next_command)
        return false;
    if (c->pending || (type == ST2_START && c->stop != ST2_STOP_NONE))
        return false;
    unsigned state = c->flags & 7u;
    if ((type == ST2_START && state != ST_IDLE) || (type == ST2_STOP && state > ST_STOPPING))
        return false;
    /* STOP at IDLE is also a command barrier, confirmed by its matching ACK. */
    if (type == ST2_START)
    {
        StTaskMode mode = c->task.mode ? c->task.mode : ST_TASK_CAPTURE;
        if (!Task_IsRecordingMode(mode) ||
            Task_Begin(&c->task, mode, c->next_command) != ST_TASK_OK)
            return false;
        c->output_started = false;
    }
    else
        Task_RequestStop(&c->task, c->next_command);
    begin_command(c, type, now);
    return true;
}
static bool Link_BeginQuery(st2_client *c, uint8_t type, uint32_t now)
{
    if (!c || c->pending || !c->next_command || !st2_client_fresh(c, now))
        return false;
    begin_command(c, type, now);
    return true;
}
StLinkRequestResult Link_QueryTask(st2_client *c, uint32_t now)
{
    return Link_BeginQuery(c, ST2_TASK_QUERY, now) ? ST_LINK_REQUEST_OK : ST_LINK_REQUEST_REJECTED;
}
StLinkRequestResult Link_QueryDetails(st2_client *c, uint32_t now)
{
    if (!Link_BeginQuery(c, ST2_DETAILS_QUERY, now))
        return ST_LINK_REQUEST_REJECTED;
    c->request.section = 1;
    c->details = (st2_details){.epoch = c->epoch, .id = c->request.id, .section = 1};
    return ST_LINK_REQUEST_OK;
}
StLinkRequestResult Link_RequestSensor(st2_client *c, uint8_t action, bool enabled, uint32_t now)
{
    if (action && c && c->need_recovery)
        return ST_LINK_REQUEST_REJECTED;
    if (!action)
        return Link_BeginQuery(c, ST2_SENSOR_QUERY, now) ? ST_LINK_REQUEST_OK
                                                         : ST_LINK_REQUEST_REJECTED;
    if (action < 7 || action > 8 || !Link_BeginQuery(c, ST2_SENSOR_COMMAND, now))
        return ST_LINK_REQUEST_REJECTED;
    c->request.data[0] = action;
    c->request.data[1] = enabled ? 1 : 0;
    c->sensor_have_view = false;
    return ST_LINK_REQUEST_OK;
}
bool st2_client_cal_command(st2_client *c, const uint8_t data[14], uint32_t now)
{
    if (data && c->need_recovery)
        return false;
    /* Read-only polls can be superseded by an explicit user action. A mutation
     * already in flight must complete or hit its bounded timeout first. */
    if (c->pending && c->request.type == ST2_CAL_QUERY && data)
        c->pending = false;
    if (c->stop != ST2_STOP_NONE || c->pending || !st2_client_fresh(c, now) || !c->next_command ||
        c->timing.operation_ms < c->timing.ack_rto_ms + 4u * c->timing.max_airtime_ms ||
        (data && (c->flags & 7u) != ST_IDLE))
        return false;
    st2_packet p = {.type = data ? ST2_CAL_COMMAND : ST2_CAL_QUERY,
                    .epoch = c->epoch,
                    .id = c->next_command,
                    .pair_id = c->pair_id};
    if (data)
        memcpy(p.data, data, 14);
    uint8_t wire[ST2_MAX];
    if (!st2_encode(&p, wire))
        return false;
    c->request = p;
    c->next_command = c->next_command == ST_TRANSACTION_LIMIT ? 0 : c->next_command + 1u;
    c->pending = true;
    c->transaction = ST_TX_WAIT_ACK;
    c->attempts = 0;
    c->have_sent = false;
    c->waiting_tx_done = false;
    c->started_ms = now;
    c->cal_seen = 0;
    c->cal_have_view = false;
    return true;
}
/* now/started/last_send are modulo-2^32 milliseconds. Configured intervals are
 * bounded well below half the timer range. Total deadline also bounds retries
 * when an ACCEPTED reply resets the per-round retry budget. */
bool st2_client_tx_due(st2_client *c, uint32_t now, uint8_t out[ST2_MAX], size_t *n)
{
    if (c->stop == ST2_STOP_QUEUED && c->link == ST2_LINK_UP)
    {
        if ((uint32_t)(now - c->stop_requested_ms) > c->timing.operation_ms)
        {
            c->stop = ST2_STOP_UNCONFIRMED;
            c->pending = false;
            c->transaction = ST_TX_TIMEOUT;
            c->task.state = ST_TASK_UNKNOWN;
            return false;
        }
        if (!st2_client_fresh(c, now) || c->waiting_tx_done)
            return false; /* Do not replay START while stop intent is queued. */
        begin_command(c, ST2_STOP, c->stop_requested_ms);
        Task_RequestStop(&c->task, c->request.id);
    }
    if (c->link == ST2_LINK_DOWN || (c->link == ST2_LINK_UP && !c->pending))
        return false;
    if ((uint32_t)(now - c->started_ms) > c->timing.operation_ms)
    {
        if (c->pending)
        {
            c->pending = false;
            c->transaction = ST_TX_TIMEOUT;
            if (c->request.type == ST2_START || c->request.type == ST2_STOP)
                c->task.state = ST_TASK_UNKNOWN;
            if (c->request.type == ST2_STOP)
                c->stop = ST2_STOP_UNCONFIRMED;
        }
        else
            c->link = ST2_LINK_DOWN;
        return false;
    }
    if (c->waiting_tx_done || c->attempts >= c->timing.attempts)
        return false;
    uint32_t reply_window = c->timing.ack_rto_ms;
    if (c->request.type == ST2_CAL_COMMAND || c->request.type == ST2_CAL_QUERY)
        reply_window +=
            3u * c->timing.max_airtime_ms; /* four fragments get one complete reply window */
    if (c->have_sent && (uint32_t)(now - c->last_send_ms) < reply_window + c->retry_jitter_ms)
        return false;
    *n = st2_encode(&c->request, out);
    return *n != 0;
}
StLinkHealth Link_GetHealth(bool have, uint32_t received, uint32_t now, bool driver_fault)
{
    if (driver_fault)
        return ST_LINK_DRIVER_FAULT;
    if (!have)
        return ST_LINK_WAIT;
    uint32_t age = now - received;
    return age >= ST_LINK_LOST_MS   ? ST_LINK_LOST
           : age >= ST_LINK_SLOW_MS ? ST_LINK_SLOW
                                    : ST_LINK_OK;
}
bool Link_GetLed(StLinkHealth health, uint32_t now)
{
    if (health == ST_LINK_OK)
        return true;
    if (health == ST_LINK_SLOW)
        return (now / ST_LINK_LED_SLOW_HALF_MS) % 2u == 0;
    if (health == ST_LINK_LOST)
        return (now / ST_LINK_LED_FAST_HALF_MS) % 2u == 0;
    if (health == ST_LINK_DRIVER_FAULT)
    {
        uint32_t phase = now % 1000u;
        return phase < 100u || (phase >= 200u && phase < 300u);
    }
    return false;
}
void st2_client_tx_accepted(st2_client *c)
{
    c->waiting_tx_done = true;
    c->have_sent = true;
    c->attempts++;
}
void st2_client_tx_done(st2_client *c, uint32_t now, uint32_t jitter)
{
    if (c->waiting_tx_done)
    {
        c->waiting_tx_done = false;
        c->last_send_ms = now;
        c->retry_jitter_ms = jitter % 251u;
    }
}

size_t st2_details_begin(st2_details *d, uint32_t epoch, uint32_t id, uint8_t out[ST2_MAX])
{
    *d = (st2_details){.epoch = epoch, .id = id, .section = 1};
    st2_packet p = {.type = ST2_DETAILS_QUERY, .epoch = epoch, .id = id, .section = 1};
    return st2_encode(&p, out);
}
bool st2_details_receive(st2_details *d, const uint8_t *in, size_t n)
{
    st2_packet p;
    if (!st2_decode(&p, in, n) || p.type != ST2_DETAILS_REPLY || p.epoch != d->epoch ||
        p.id != d->id || p.section != d->section || (p.fragment != 0x10 && p.fragment != 0x11))
        return false;
    unsigned index = p.fragment & 1u;
    uint8_t bit = (uint8_t)(1u << index);
    if (p.count != (index ? 7 : 16))
        return false;
    if (d->seen & bit)
        return !memcmp(d->data + index * 16, p.data, p.count);
    memcpy(d->data + index * 16, p.data, p.count);
    d->seen |= bit;
    d->complete = d->seen == 3;
    return true;
}
