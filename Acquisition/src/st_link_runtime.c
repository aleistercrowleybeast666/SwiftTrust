#include "st_link_runtime.h"
#include <string.h>
enum
{
    ROLE_SERVER = 1,
    ROLE_CLIENT = 2
};
bool st_link_runtime_init(st_link_runtime *l, st_radio *r)
{
    if (!l || !r || r->failed || !r->config.verified || r->tx_pending || r->tx_active)
        return false;
    *l = (st_link_runtime){
        .radio = r, .observed_done = r->tx_done, .observed_timeouts = r->tx_timeouts};
    return true;
}
static bool role(st_link_runtime *l, uint8_t wanted)
{
    if (!l || !l->radio || l->fault)
        return false;
    if (l->role && l->role != wanted)
    {
        l->fault = true;
        return false;
    }
    l->role = wanted;
    return true;
}
static bool send_ready(st_link_runtime *l)
{
    return !l->radio->failed && !l->radio->tx_pending && !l->radio->tx_active;
}
void st_link_server_step(st_link_runtime *l, st2_server *s, st2_measurement m, uint64_t now)
{
    if (!s || !role(l, ROLE_SERVER))
        return;
    st_radio_tick(l->radio, (uint32_t)(now / 1000));
    if (l->radio->failed)
    {
        l->fault = true;
        return;
    }
    if (!l->length)
    {
        uint8_t rx[49];
        size_t n = 0, out = 0;
        if (st_radio_receive(l->radio, rx, &n))
        {
            if (l->auto_link && (rx[0] == AUTO_LINK_DISCOVER || rx[0] == AUTO_LINK_ADVERTISE))
                out = AutoLink_Receive(l->auto_link, rx, n, l->packet);
            else
                out = st2_server_receive(s, rx, n, m, now, l->packet);
        }
        if (!out)
            out = st2_server_poll(s, m, now, l->packet);
        l->length = (uint8_t)out;
    }
    if (l->length && send_ready(l) && st_radio_send(l->radio, l->packet, l->length))
        l->length = 0;
}
void st_link_client_step(st_link_runtime *l, st2_client *c, uint32_t now, uint32_t jitter)
{
    if (!c || !role(l, ROLE_CLIENT))
        return;
    st_radio *r = l->radio;
    IgnitionClient *ignition = c->ignition;
    if (ignition)
        (void)IgnitionClient_SetSession(ignition, c->epoch, c->link == ST2_LINK_UP);
    uint8_t due[ST2_MAX];
    size_t n = 0;
    (void)st2_client_tx_due(c, now, due, &n); /* apply total deadline before any queued TX */
    if (ignition)
        (void)IgnitionClient_TxDue(ignition, now, due, &n);
    if (l->awaiting_tx && r->tx_pending && !r->tx_active)
    {
        uint8_t current[ST2_MAX];
        size_t size = st2_encode(&c->request, current);
        bool live = l->ignition_tx
                        ? IgnitionClient_RequestMatches(ignition, l->packet, l->length)
                        : c->link != ST2_LINK_DOWN && (c->link != ST2_LINK_UP || c->pending);
        if (!l->ignition_tx && ignition && ignition->pending &&
            ignition->request[7] == IGNITION_DISARM)
            live = false; /* Reorder only an unsent packet; on-air traffic cannot be recalled. */
        if (l->ignition_tx)
            size = 0; /* The authenticated request is retained byte-for-byte. */
        if (!live || (!l->ignition_tx &&
            (size != l->length || memcmp(current, l->packet, size))))
        {
            if (st_radio_cancel_queued(r, l->packet, l->length))
            {
                if (l->ignition_tx && ignition)
                    ignition->waiting_tx = false;
                else
                    c->waiting_tx_done = false;
            }
            l->awaiting_tx = false;
            l->length = 0;
        }
    }
    st_radio_tick(r, now);
    if (r->tx_done != l->observed_done || r->tx_timeouts != l->observed_timeouts)
    {
        if (l->awaiting_tx)
        {
            if (l->ignition_tx)
                IgnitionClient_TxDone(ignition, l->packet, l->length, now, jitter,
                                      r->tx_timeouts != l->observed_timeouts);
            else
                st2_client_tx_done(c, now, jitter);
        }
        l->awaiting_tx = false;
        l->length = 0;
    }
    l->observed_done = r->tx_done;
    l->observed_timeouts = r->tx_timeouts;
    if (r->failed)
    {
        if (ignition)
            (void)IgnitionClient_SetSession(ignition, 0, false);
        l->fault = true;
        c->link = ST2_LINK_DOWN;
        c->have_status = false;
        if (c->pending)
            c->transaction = ST_TX_TIMEOUT;
        c->pending = false;
        c->waiting_tx_done = false;
        if (c->stop != ST2_STOP_NONE)
            c->stop = ST2_STOP_UNCONFIRMED;
        return;
    }
    uint8_t rx[49];
    if (st_radio_receive(r, rx, &n))
    {
        if (l->auto_link && (rx[0] == AUTO_LINK_DISCOVER || rx[0] == AUTO_LINK_ADVERTISE))
            (void)AutoLink_Receive(l->auto_link, rx, n, due);
        else if (ignition && rx[0] == ST2_IGN_REPLY)
            IgnitionClient_Receive(ignition, rx, n, now);
        else
            (void)st2_client_receive(c, rx, n, now);
    }
    if (ignition)
        (void)IgnitionClient_SetSession(ignition, c->epoch, c->link == ST2_LINK_UP);
    n = 0;
    bool ignition_due = ignition && ignition->pending &&
                         (ignition->request[7] == IGNITION_DISARM ||
                          (c->link == ST2_LINK_UP && !c->pending && c->stop == ST2_STOP_NONE));
    bool transmit = ignition_due ? IgnitionClient_TxDue(ignition, now, due, &n)
                                 : st2_client_tx_due(c, now, due, &n);
    if (!transmit && l->auto_link && send_ready(l))
    {
        if (AutoLink_TxDue(l->auto_link, now, due, &n))
            (void)st_radio_send(r, due, n);
        return;
    }
    if (transmit && send_ready(l) && st_radio_send(r, due, n))
    {
        memcpy(l->packet, due, n);
        l->length = (uint8_t)n;
        l->awaiting_tx = true;
        l->ignition_tx = ignition_due;
        if (ignition_due)
            IgnitionClient_TxAccepted(ignition, due, n, now);
        else
            st2_client_tx_accepted(c);
    }
}
