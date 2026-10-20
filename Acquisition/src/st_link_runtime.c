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
            out = st2_server_receive(s, rx, n, m, now, l->packet);
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
    uint8_t due[ST2_MAX];
    size_t n = 0;
    (void)st2_client_tx_due(c, now, due, &n); /* apply total deadline before any queued TX */
    if (l->awaiting_tx && r->tx_pending && !r->tx_active)
    {
        uint8_t current[ST2_MAX];
        size_t size = st2_encode(&c->request, current);
        bool live = c->link != ST2_LINK_DOWN && (c->link != ST2_LINK_UP || c->pending);
        if (!live || size != l->length || memcmp(current, l->packet, size))
        {
            if (st_radio_cancel_queued(r, l->packet, l->length))
                c->waiting_tx_done = false; /* Canceled unsent copy has no future TX_DONE. */
            l->awaiting_tx = false;
            l->length = 0;
        }
    }
    st_radio_tick(r, now);
    if (r->tx_done != l->observed_done || r->tx_timeouts != l->observed_timeouts)
    {
        if (l->awaiting_tx)
            st2_client_tx_done(c, now, jitter);
        l->awaiting_tx = false;
        l->length = 0;
    }
    l->observed_done = r->tx_done;
    l->observed_timeouts = r->tx_timeouts;
    if (r->failed)
    {
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
        (void)st2_client_receive(c, rx, n, now);
    n = 0;
    if (st2_client_tx_due(c, now, due, &n) && send_ready(l) && st_radio_send(r, due, n))
    {
        memcpy(l->packet, due, n);
        l->length = (uint8_t)n;
        l->awaiting_tx = true;
        st2_client_tx_accepted(c);
    }
}
