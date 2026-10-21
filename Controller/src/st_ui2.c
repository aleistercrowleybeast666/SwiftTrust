#include "st_ui.h"
#include <stdio.h>
static bool IgnitionUi_IsPage(st_ui_page page)
{
    return page == ST_UI_IGNITION_ONLY || page == ST_UI_COMBINED ||
           page == ST_UI_IGNITION_CONFIRM || page == ST_UI_COMBINED_CONFIRM ||
           page == ST_UI_FIRE_CONFIRM;
}
static const char *IgnitionUi_GetNotice(const IgnitionClient *ignition)
{
    if (!ignition->auth.key_bound)
        return "KEY NOT SET";
    if (!ignition->epoch)
        return "CONNECT REQUIRED";
    if (ignition->pending)
        return "WAIT AUTH RESPONSE";
    if (ignition->operation == IGNITION_OPERATION_UNKNOWN)
        return "RESULT UNKNOWN";
    if (ignition->request_result == IGNITION_REQUEST_BUSY)
        return "BUSY / WAIT COMMAND";
    if (ignition->request_result == IGNITION_REQUEST_EXHAUSTED)
        return "RECONNECT REQUIRED";
    if (ignition->request_result == IGNITION_REQUEST_NO_PROFILE ||
        (ignition->have_reply && ignition->reason == IGNITION_REASON_UNBOUND))
        return "HW NOT BOUND";
    if (ignition->operation == IGNITION_OPERATION_REJECTED)
        return "REQUEST REJECTED";
    if (ignition->state == IGNITION_FIRING)
        return "MCU PULSE ACTIVE";
    if (ignition->state == IGNITION_LOCKOUT)
        return "PULSE ENDED / STOP";
    if (ignition->have_grant)
        return "ARMED / CONFIRM FIRE";
    if (ignition->state == IGNITION_SAFE)
        return "MCU OUTPUT LOW";
    return "WAIT SIGNAL STATUS";
}
static bool IgnitionUi_Stop(st_ui *u, st2_client *c, uint32_t now, bool capture)
{
    u->combined_starting = false;
    u->ignition_target = 0;
    bool accepted = IgnitionClient_RequestDisarm(c->ignition, now) == IGNITION_REQUEST_OK;
    if (capture)
        accepted = st2_client_command(c, ST2_STOP, now) || accepted;
    return accepted;
}
void st_ui2_service(st_ui *u, st2_client *c, uint32_t now)
{
    IgnitionClient *ignition = c->ignition;
    if (!ignition)
        return;
    if (ignition->have_grant && (int32_t)(now - ignition->grant_deadline_ms) >= 0)
        ignition->have_grant = false;
    if (u->combined_starting)
    {
        if (!st2_client_fresh(c, now) || c->transaction == ST_TX_REJECTED ||
            c->transaction == ST_TX_TIMEOUT || c->transaction == ST_TX_REBOOT ||
            (c->flags & 7u) == ST_FAULT)
        {
            (void)IgnitionUi_Stop(u, c, now, true);
            return;
        }
        if (!c->pending && (c->flags & 7u) == ST_CAPTURING && !ignition->pending)
        {
            u->combined_starting = false;
            if (IgnitionClient_RequestArm(ignition, 2, ignition->profile_id, now) != IGNITION_REQUEST_OK)
                (void)IgnitionUi_Stop(u, c, now, true);
        }
    }
    if (ignition->have_grant && !ignition->pending &&
        (u->page == ST_UI_IGNITION_ONLY || u->page == ST_UI_COMBINED))
    {
        u->ignition_mode = ignition->mode;
        u->page = ST_UI_FIRE_CONFIRM;
        u->selection = 1; /* A grant never turns the next stale press into FIRE. */
    }
    if (IgnitionUi_IsPage(u->page) && !ignition->have_grant && !ignition->pending &&
        !u->combined_starting && !c->pending && c->stop == ST2_STOP_NONE &&
        st2_client_fresh(c, now) && (uint32_t)(now - u->ignition_poll_ms) >= 1500)
    {
        u->ignition_poll_ms = now;
        (void)IgnitionClient_Query(ignition, !ignition->profile_id, u->ignition_target, now);
    }
}
bool st_ui2_handle(st_ui *u, st_ui_event e, st2_client *c, uint32_t now)
{
    u->notice = ST_UI_NOTICE_NONE;
    IgnitionClient *ignition = c->ignition;
    if (ignition && u->page == ST_UI_FIRE_CONFIRM)
    {
        if (e == ST_UI_NEXT || e == ST_UI_PREV)
        {
            u->selection ^= 1u;
            return false;
        }
        if (e == ST_UI_PRESS || e == ST_UI_BACK)
        {
            bool fire = e == ST_UI_PRESS && u->selection == 0;
            u->page = u->ignition_mode == 2 ? ST_UI_COMBINED : ST_UI_IGNITION_ONLY;
            u->selection = fire ? 1u : 0u;
            if (!fire)
                return IgnitionUi_Stop(u, c, now, u->ignition_mode == 2);
            if (!st2_client_fresh(c, now))
                return IgnitionUi_Stop(u, c, now, u->ignition_mode == 2);
            bool accepted = IgnitionClient_RequestFire(ignition, now) == IGNITION_REQUEST_OK;
            if (accepted)
                u->ignition_target = st_get16(ignition->request + 5);
            return accepted;
        }
        return false;
    }
    if (ignition && e == ST_UI_BACK && IgnitionUi_IsPage(u->page))
        (void)IgnitionUi_Stop(u, c, now,
                              u->page == ST_UI_COMBINED || u->page == ST_UI_COMBINED_CONFIRM);
    if (ignition && e == ST_UI_PRESS &&
        (u->page == ST_UI_IGNITION_ONLY || u->page == ST_UI_COMBINED) && u->selection == 2)
        (void)IgnitionUi_Stop(u, c, now, u->page == ST_UI_COMBINED);
    if (ignition && e == ST_UI_PRESS &&
        (u->page == ST_UI_IGNITION_ONLY || u->page == ST_UI_COMBINED) && u->selection == 1)
    {
        u->unavailable = false;
        return IgnitionUi_Stop(u, c, now, u->page == ST_UI_COMBINED);
    }
    if (ignition && e == ST_UI_PRESS &&
        (u->page == ST_UI_IGNITION_CONFIRM || u->page == ST_UI_COMBINED_CONFIRM) &&
        u->selection == 0)
    {
        uint8_t mode = u->page == ST_UI_IGNITION_CONFIRM ? 1 : 2;
        st_client disabled = {0};
        (void)st_ui_handle(u, e, &disabled, now);
        u->unavailable = false;
        if (!st2_client_fresh(c, now) || c->pending || c->stop != ST2_STOP_NONE)
        {
            u->notice = st2_client_fresh(c, now) ? ST_UI_NOTICE_BUSY : ST_UI_NOTICE_LINK;
            return false;
        }
        u->ignition_mode = mode;
        u->ignition_target = 0;
        if (mode == 2 && ignition->profile_id && (c->flags & 7u) == ST_IDLE)
        {
            u->combined_starting = st2_client_command(c, ST2_START, now);
            return u->combined_starting;
        }
        /* Profile is learned from authenticated board capabilities. */
        IgnitionRequestResult result = ignition->profile_id
            ? IgnitionClient_RequestArm(ignition, mode, ignition->profile_id, now)
            : IgnitionClient_Query(ignition, true, 0, now);
        return result == IGNITION_REQUEST_OK;
    }
    if (u->page == ST_UI_CALIBRATION)
    {
        bool idle = st2_client_fresh(c, now) && (c->flags & 7u) == ST_IDLE &&
                    (!c->pending || c->request.type == ST2_CAL_QUERY);
        bool exit = u->calibration ? st_cal_ui_handle(u->calibration, (st_cal_ui_event)e, idle)
                                   : (e == ST_UI_BACK || e == ST_UI_PRESS);
        if (exit)
        {
            u->page = ST_UI_CAPTURE;
            u->selection = 2;
        }
        return false;
    }
    if (u->page == ST_UI_STATUS)
    {
        if (e == ST_UI_NEXT || e == ST_UI_PREV)
        {
            u->selection ^= 1u;
            return false;
        }
        if (e == ST_UI_PRESS && u->selection == 1)
        {
            u->unavailable = false;
            if (!st2_client_connect(c, now))
                u->notice = ST_UI_NOTICE_CONNECT;
            return false;
        }
    }
    /* Navigation is identical, but only the R2 command API can create RF commands. */
    bool stop = e == ST_UI_PRESS && u->page == ST_UI_CAPTURE && u->selection == 1;
    bool start = e == ST_UI_PRESS && u->page == ST_UI_CONFIRM && u->selection == 0;
    st_ui_page previous_page = u->page;
    st_client disabled = {0};
    (void)st_ui_handle(u, e, &disabled, now);
    if (ignition && previous_page == ST_UI_HOME && IgnitionUi_IsPage(u->page) &&
        st2_client_fresh(c, now) && !c->pending && c->stop == ST2_STOP_NONE)
        (void)IgnitionClient_Query(ignition, true, 0, now);
    if (u->page == ST_UI_CALIBRATION && u->calibration)
        st_cal_ui_open(u->calibration);
    if (start || stop)
    {
        bool ok = st2_client_command(c, start ? ST2_START : ST2_STOP, now);
        u->unavailable = false;
        if (!ok)
            u->notice = !st2_client_fresh(c, now) ? ST_UI_NOTICE_LINK
                        : c->pending              ? ST_UI_NOTICE_BUSY
                        : !c->next_command        ? ST_UI_NOTICE_RECONNECT
                                                  : ST_UI_NOTICE_STATE;
        return ok;
    }
    return false;
}
void st_ui2_render(const st_ui *u, const st2_client *c, st_voltage_view own, uint32_t now,
                   st_ui_model *m)
{
    bool fresh = st2_client_fresh(c, now);
    st_client view = {.known = c->have_status,
                      .received_ms = fresh ? now : now - 3001u,
                      .transaction = c->transaction};
    view.status.state = c->flags & 7u;
    unsigned tf = (c->flags >> 3) & 3u;
    view.status.storage = (uint8_t)(tf == ST2_TF_FAULT ? 4 : (tf == ST2_TF_OPEN ? 1 : 0));
    view.status.mv = c->mv;
    view.status.voltage_flags = c->mv == 65535 ? 0 : 3;
    view.status.voltage_age_ms = (c->flags & 32u) ? 0 : 65535;
    st_ui_render(u, &view, own, now, m);
    const char *notice = NULL;
    switch (u->notice)
    {
    case ST_UI_NOTICE_BUSY:
        notice = "BUSY / WAIT COMMAND";
        break;
    case ST_UI_NOTICE_LINK:
        notice = "LINK LOST / CONNECT";
        break;
    case ST_UI_NOTICE_STATE:
        notice = "STATE UNAVAILABLE";
        break;
    case ST_UI_NOTICE_RECONNECT:
        notice = "RECONNECT REQUIRED";
        break;
    case ST_UI_NOTICE_CONNECT:
        notice = "CONNECT FAILED";
        break;
    default:
        break;
    }
    if (c->stop == ST2_STOP_QUEUED)
        notice = fresh ? "STOP QUEUED" : "LINK LOST / STOP ?";
    else if (c->stop == ST2_STOP_ACTIVE)
        notice = fresh ? "WAIT STOP CONFIRM" : "LINK LOST / STOP ?";
    else if (c->stop == ST2_STOP_UNCONFIRMED)
        notice = c->transaction == ST_TX_REJECTED ? "STOP FAILED / CHECK" : "STOP UNCONFIRMED";
    if (notice && u->page != ST_UI_STATUS)
        (void)snprintf(m->rows[7], 22, "%s", notice);
    if (u->page == ST_UI_CALIBRATION && u->calibration)
    {
        st_cal_panel panel;
        st_cal_ui_render(u->calibration, &panel);
        for (unsigned i = 0; i < 6; i++)
            for (unsigned j = 0; j < 22; j++)
                m->rows[i + 2][j] = panel.rows[i][j];
        m->inverse = panel.inverse;
        m->inverse_row = (uint8_t)(panel.inverse_row + 2u);
        m->inverse_column = panel.inverse_column;
    }
    if (u->page == ST_UI_STATUS)
    {
        if (notice)
            (void)snprintf(m->rows[2], 22, "%s", notice);
        else if (!c->have_status || c->mv == UINT16_MAX)
            (void)snprintf(m->rows[2], 22, "ACQ BAT --");
        else
            (void)snprintf(m->rows[2], 22, "ACQ BAT %u.%02uV%s", (unsigned)c->mv / 1000u,
                           ((unsigned)c->mv % 1000u) / 10u,
                           fresh && (c->flags & 32u) ? "" : " OLD");
        const char *names[] = {"UNKNOWN", "READY/CLOSED", "OPEN", "ERROR"};
        (void)snprintf(m->rows[4], 22, "TF %s", c->have_status ? names[tf] : "UNKNOWN");
        (void)snprintf(m->rows[5], 22, "QUALITY %s",
                       !c->have_status ? "UNKNOWN" : ((c->flags & 128u) ? "CHECK DETAILS" : "OK"));
        (void)snprintf(m->rows[5], 22, "ADC %s%s",
                       !c->have_status ? "UNKNOWN"
                                       : ((fresh && (c->flags & 64u)) ? "FRESH" : "OLD/NO DATA"),
                       (c->flags & 128u) ? " !" : "");
        (void)snprintf(m->rows[6], 22, "%c BACK", u->selection == 0 ? '>' : ' ');
        (void)snprintf(m->rows[7], 22, "%c CONNECT", u->selection == 1 ? '>' : ' ');
    }
    if (c->ignition && IgnitionUi_IsPage(u->page))
    {
        if (!notice)
            (void)snprintf(m->rows[7], 22, "%s", IgnitionUi_GetNotice(c->ignition));
        if (u->page == ST_UI_IGNITION_ONLY || u->page == ST_UI_COMBINED)
        {
            (void)snprintf(m->rows[3], 22, "%c START", u->selection == 0 ? '>' : ' ');
            (void)snprintf(m->rows[4], 22, "%c STOP", u->selection == 1 ? '>' : ' ');
            (void)snprintf(m->rows[5], 22, "EXT FEEDBACK UNKNOWN");
        }
        if (u->combined_starting)
            (void)snprintf(m->rows[7], 22, "WAIT CAPTURE READY");
        if (u->page == ST_UI_FIRE_CONFIRM)
        {
            uint32_t remaining = c->ignition->have_grant &&
                (int32_t)(c->ignition->grant_deadline_ms - now) > 0
                ? (c->ignition->grant_deadline_ms - now) / 1000u : 0;
            (void)snprintf(m->rows[2], 22, "FIRE 5s? ARM %lus", (unsigned long)remaining);
            (void)snprintf(m->rows[3], 22, "%c CONFIRM FIRE", u->selection == 0 ? '>' : ' ');
            (void)snprintf(m->rows[4], 22, "%c BACK / DISARM", u->selection == 1 ? '>' : ' ');
            (void)snprintf(m->rows[5], 22, "EXT FEEDBACK UNKNOWN");
            m->rows[6][0] = '\0';
        }
    }
}
