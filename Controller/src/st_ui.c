#include "st_ui.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
void st_ui_attach_calibration(st_ui *u, st_cal_ui *cal)
{
    u->calibration = cal;
}
void st_ui_init(st_ui *u)
{
    *u = (st_ui){.page = ST_UI_HOME};
}
static unsigned count(const st_ui *u)
{
    switch (u->page)
    {
    case ST_UI_HOME:
        return 4;
    case ST_UI_CAPTURE:
        return 4;
    case ST_UI_SETTINGS:
        return 3;
    case ST_UI_CONFIRM:
        return 2;
    default:
        return 1;
    }
}
bool st_ui_handle(st_ui *u, st_ui_event e, st_client *c, uint32_t now)
{
    u->unavailable = false;
    if (e == ST_UI_BACK)
    {
        if (u->page == ST_UI_CALIBRATION)
        {
            u->page = ST_UI_CAPTURE;
            u->selection = 2;
            return false;
        }
        if (u->page == ST_UI_CONFIRM)
        {
            u->page = ST_UI_CAPTURE;
            u->selection = u->return_selection;
        }
        else
        {
            u->selection =
                u->page == ST_UI_IGNITION_ONLY
                    ? 1
                    : (u->page == ST_UI_CAPTURE ? 2 : (u->page == ST_UI_COMBINED ? 3 : 0));
            u->page = ST_UI_HOME;
        }
        return false;
    }
    unsigned n = count(u);
    if (e == ST_UI_NEXT)
    {
        u->selection = (u->selection + 1) % n;
        return false;
    }
    if (e == ST_UI_PREV)
    {
        u->selection = (u->selection + n - 1) % n;
        return false;
    }
    if (e != ST_UI_PRESS)
        return false;
    switch (u->page)
    {
    case ST_UI_HOME:
    {
        static const st_ui_page pages[] = {ST_UI_STATUS, ST_UI_IGNITION_ONLY, ST_UI_CAPTURE,
                                           ST_UI_COMBINED};
        u->page = pages[u->selection % 4u];
        u->selection = 0;
        return false;
    }
    case ST_UI_CAPTURE:
        if (u->selection == 0)
        {
            u->return_selection = 0;
            u->page = ST_UI_CONFIRM;
            u->selection = 1;
            return false;
        }
        if (u->selection == 1)
            return st_client_command(c, ST_MSG_STOP, now);
        if (u->selection == 2)
        {
            u->page = ST_UI_CALIBRATION;
            u->selection = 0;
            return false;
        }
        u->page = ST_UI_HOME;
        u->selection = 2;
        return false;
    case ST_UI_CONFIRM:
        u->page = ST_UI_CAPTURE;
        if (u->selection == 0)
        {
            u->selection = 0;
            return st_client_command(c, ST_MSG_START, now);
        }
        u->selection = 0;
        return false;
    case ST_UI_CALIBRATION:
        u->page = ST_UI_CAPTURE;
        u->selection = 2;
        return false;
    case ST_UI_SETTINGS:
        if (u->selection < 2)
        {
            u->unavailable = true;
            return false;
        }
        u->page = ST_UI_HOME;
        u->selection = 2;
        return false;
    case ST_UI_IGNITION_ONLY:
        u->page = ST_UI_HOME;
        u->selection = 1;
        return false;
    case ST_UI_COMBINED:
        u->page = ST_UI_HOME;
        u->selection = 3;
        return false;
    default:
        u->page = ST_UI_HOME;
        u->selection = 0;
        return false;
    }
}
static void voltage(char *dst, size_t n, const char *label, uint32_t mv, bool valid, bool stale)
{
    if (!valid)
        (void)snprintf(dst, n, "%s --", label);
    else
        (void)snprintf(dst, n, "%s %lu.%02luV%s", label, (unsigned long)(mv / 1000),
                       (unsigned long)((mv % 1000) / 10), stale ? " OLD" : "");
}
static const char *state_name(unsigned s)
{
    static const char *const names[] = {"IDLE", "STARTING", "REC", "STOPPING", "FAULT"};
    return s <= ST_FAULT ? names[s] : "UNKNOWN";
}
void st_ui_render(const st_ui *u, const st_client *c, st_voltage_view own, uint32_t now,
                  st_ui_model *m)
{
    memset(m, 0, sizeof(*m));
    bool fresh = st_client_fresh(c, now);
    (void)snprintf(m->rows[0], 22, "LINK %s", fresh ? "OK" : "LOST");
    voltage(m->rows[1], 22, "SELF", own.mv, own.kind != ST_V_UNKNOWN, own.kind == ST_V_STALE);
    const char *tx = "";
    switch (c->transaction)
    {
    case ST_TX_WAIT_ACK:
        tx = "SENDING / WAIT ACK";
        break;
    case ST_TX_WAIT_STATE:
        tx = "WAIT ACTUAL STATE";
        break;
    case ST_TX_COMPLETE:
        tx = "STATE CONFIRMED";
        break;
    case ST_TX_REJECTED:
        tx = "REQUEST REJECTED";
        break;
    case ST_TX_TIMEOUT:
        tx = "TIMEOUT / CHECK STATE";
        break;
    case ST_TX_REBOOT:
        tx = "PEER REBOOTED";
        break;
    default:
        break;
    }
    switch (u->page)
    {
    case ST_UI_HOME:
        (void)snprintf(m->rows[2], 22, "SWIFTTHRUST");
        (void)snprintf(m->rows[3], 22, "%c STATUS", u->selection == 0 ? '>' : ' ');
        (void)snprintf(m->rows[4], 22, "%c IGNITION ONLY", u->selection == 1 ? '>' : ' ');
        (void)snprintf(m->rows[5], 22, "%c CAPTURE ONLY", u->selection == 2 ? '>' : ' ');
        (void)snprintf(m->rows[6], 22, "%c IGNITION + CAPTURE", u->selection == 3 ? '>' : ' ');
        break;
    case ST_UI_CAPTURE:
        (void)snprintf(m->rows[2], 22, "ACTUAL %s%s",
                       c->known ? state_name(c->status.state) : "UNKNOWN", fresh ? "" : " OLD");
        (void)snprintf(m->rows[3], 22, "%c START", u->selection == 0 ? '>' : ' ');
        (void)snprintf(m->rows[4], 22, "%c STOP", u->selection == 1 ? '>' : ' ');
        (void)snprintf(m->rows[5], 22, "%c CALIBRATION", u->selection == 2 ? '>' : ' ');
        (void)snprintf(m->rows[6], 22, "%c BACK", u->selection == 3 ? '>' : ' ');
        break;
    case ST_UI_CONFIRM:
        (void)snprintf(m->rows[2], 22, "START CAPTURE?");
        (void)snprintf(m->rows[3], 22, "%c CONFIRM", u->selection == 0 ? '>' : ' ');
        (void)snprintf(m->rows[4], 22, "%c BACK", u->selection == 1 ? '>' : ' ');
        break;
    case ST_UI_IGNITION_ONLY:
    case ST_UI_COMBINED:
        (void)snprintf(m->rows[2], 22, "%s",
                       u->page == ST_UI_IGNITION_ONLY ? "IGNITION ONLY" : "IGNITION + CAPTURE");
        (void)snprintf(m->rows[3], 22, "NOT IMPLEMENTED");
        (void)snprintf(m->rows[4], 22, "VIEW ONLY");
        (void)snprintf(m->rows[6], 22, "> BACK");
        break;
    case ST_UI_CALIBRATION:
        (void)snprintf(m->rows[2], 22, "CALIBRATION");
        (void)snprintf(m->rows[3], 22, "SERVICE UNBOUND");
        (void)snprintf(m->rows[6], 22, "> BACK");
        break;
    case ST_UI_SETTINGS:
        (void)snprintf(m->rows[2], 22, "%c CALIBRATION N/A", u->selection == 0 ? '>' : ' ');
        (void)snprintf(m->rows[3], 22, "%c RATE LOCKED", u->selection == 1 ? '>' : ' ');
        (void)snprintf(m->rows[4], 22, "IGNITION N/A");
        (void)snprintf(m->rows[5], 22, "%c BACK", u->selection == 2 ? '>' : ' ');
        break;
    default:
        voltage(m->rows[2], 22, "ACQ", c->status.mv,
                c->known && (c->status.voltage_flags & 3u) == 3u,
                !fresh || st_client_voltage_age(c, now) > 3000);
        (void)snprintf(m->rows[3], 22, "CAP %s%s",
                       c->known ? state_name(c->status.state) : "UNKNOWN", fresh ? "" : " OLD");
        (void)snprintf(m->rows[4], 22, "TF %s",
                       !c->known ? "UNKNOWN"
                                 : (c->status.storage == 4
                                        ? "ERROR"
                                        : (c->status.storage == 0 ? "CLOSED" : "OPEN")));
        if (c->known && (c->status.quality_flags & 4u))
            (void)snprintf(m->rows[5], 22, "SYNC SEQ %lu", (unsigned long)c->status.durable_sample);
        else
            (void)snprintf(m->rows[5], 22, "SYNC UNKNOWN");
        if (!c->known)
            (void)snprintf(m->rows[6], 22, "ADC UNKNOWN");
        else if (!(c->status.quality_flags & 2u))
            (void)snprintf(m->rows[6], 22, "ADC NO DATA");
        else if (c->status.adc_age_ms == 65535)
            (void)snprintf(m->rows[6], 22, "ADC AGE UNKNOWN");
        else if (!fresh || c->status.adc_age_ms > 1000)
            (void)snprintf(m->rows[6], 22, "ADC DATA OLD");
        else
            (void)snprintf(m->rows[6], 22, "DROP %u ERR %u", c->status.dropped,
                           c->status.adc_errors);
        break;
    }
    (void)snprintf(m->rows[7], 22, "%s", u->unavailable ? "NOT IMPLEMENTED" : tx);
}
int32_t st_encoder_update(st_encoder *e, uint16_t counter, unsigned edges)
{
    if (!edges || edges > 16)
        return 0;
    if (!e->initialized)
    {
        e->counter = counter;
        e->initialized = true;
        return 0;
    }
    uint16_t raw = (uint16_t)(counter - e->counter);
    e->counter = counter;
    int32_t delta = raw < 32768 ? (int32_t)raw : (int32_t)raw - 65536;
    e->remainder += delta;
    int32_t steps = e->remainder / (int32_t)edges;
    e->remainder %= (int32_t)edges;
    return steps;
}
bool st_button_press(st_button *b, bool pressed, uint32_t now, uint32_t debounce)
{
    /* Boot-held input is ignored until released, then a new debounced press. */
    if (!b->initialized)
    {
        *b = (st_button){pressed, pressed, true, now};
        return false;
    }
    if (pressed != b->raw)
    {
        b->raw = pressed;
        b->changed_ms = now;
    }
    if (b->stable != b->raw && (uint32_t)(now - b->changed_ms) >= debounce)
    {
        b->stable = b->raw;
        return b->stable;
    }
    return false;
}
