#include "st_ui.h"
#include <stdio.h>
bool st_ui2_handle(st_ui *u, st_ui_event e, st2_client *c, uint32_t now)
{
    u->notice = ST_UI_NOTICE_NONE;
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
    st_client disabled = {0};
    (void)st_ui_handle(u, e, &disabled, now);
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
}
