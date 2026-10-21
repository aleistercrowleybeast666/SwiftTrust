#include "st_auto_link.h"
#include <string.h>
#define AUTO_LINK_RETRY_MS 2000u
#define AUTO_LINK_PREFIX_BYTES 15u
_Static_assert((unsigned)AUTO_LINK_DISCOVER == (unsigned)ST2_DISCOVER &&
               (unsigned)AUTO_LINK_ADVERTISE == (unsigned)ST2_ADVERTISE, "shared discovery wire types");
static bool AutoLink_IsUidValid(const uint8_t uid[12])
{
    unsigned zero = 0, ff = 0;
    for (unsigned i = 0; i < 12; i++)
    {
        zero += uid[i] == 0;
        ff += uid[i] == 255;
    }
    return zero != 12 && ff != 12;
}
static void AutoLink_MakeTag(const AutoLink *link, const uint8_t prefix[15],
                             const uint8_t target[12], uint8_t tag[16])
{
    uint8_t message[34];
    memcpy(message, "STDISC1", 7);
    memcpy(message + 7, prefix, AUTO_LINK_PREFIX_BYTES);
    memcpy(message + 22, target, 12);
    (void)Ignition_ComputeCmac(link->key, message, sizeof message, tag);
    Ignition_ClearMemory(message, sizeof message);
}
static size_t AutoLink_Encode(const AutoLink *link, uint8_t type, const uint8_t target[12],
                              uint8_t out[35])
{
    out[0] = type;
    st_put16(out + 1, link->network);
    memcpy(out + 3, link->pairing->own_uid, 12);
    AutoLink_MakeTag(link, out, target, out + AUTO_LINK_PREFIX_BYTES);
    st_put16(out + 31, st_crc16(out, 31));
    return AUTO_LINK_BYTES;
}
void AutoLink_Init(AutoLink *link, st_pairing *pairing, const uint8_t key[16], uint16_t network,
                   void *ctx, bool (*idle)(void *))
{
    if (!link)
        return;
    *link = (AutoLink){.pairing = pairing, .network = network, .ctx = ctx, .idle = idle};
    if (key)
        memcpy(link->key, key, 16);
}
bool AutoLink_TakeChanged(AutoLink *link)
{
    bool changed = link && link->changed;
    if (changed)
        link->changed = false;
    return changed;
}
bool AutoLink_TxDue(AutoLink *link, uint32_t now, uint8_t out[35], size_t *length)
{
    if (!link || !link->pairing || !out || !length || !link->network ||
        link->pairing->role != ST_NV_CONTROLLER || link->pairing->store.status != ST_NV_EMPTY ||
        (link->sent && (uint32_t)(now - link->last_send_ms) < AUTO_LINK_RETRY_MS))
        return false;
    const uint8_t broadcast[12] = {0};
    *length = AutoLink_Encode(link, AUTO_LINK_DISCOVER, broadcast, out);
    link->sent = true;
    link->last_send_ms = now;
    return true;
}
size_t AutoLink_Receive(AutoLink *link, const uint8_t *wire, size_t length, uint8_t out[35])
{
    if (!link || !link->pairing || !wire || !out || !link->network || length != AUTO_LINK_BYTES ||
        st_get16(wire + 1) != link->network || st_get16(wire + 31) != st_crc16(wire, 31) ||
        !AutoLink_IsUidValid(wire + 3) || !memcmp(wire + 3, link->pairing->own_uid, 12))
        return 0;
    st_pairing *pairing = link->pairing;
    uint8_t wanted = pairing->role == ST_NV_ACQUISITION ? AUTO_LINK_DISCOVER : AUTO_LINK_ADVERTISE;
    if (wire[0] != wanted || (wanted == AUTO_LINK_ADVERTISE && !link->sent))
        return 0;
    const uint8_t broadcast[12] = {0};
    uint8_t tag[16], difference = 0;
    AutoLink_MakeTag(link, wire, wanted == AUTO_LINK_DISCOVER ? broadcast : pairing->own_uid, tag);
    for (unsigned i = 0; i < 16; i++)
        difference |= tag[i] ^ wire[AUTO_LINK_PREFIX_BYTES + i];
    Ignition_ClearMemory(tag, sizeof tag);
    if (difference)
        return 0;
    /* Automatic provisioning is ONLY for pristine media, never damaged history.
     * A remembered peer cannot be replaced by another radio after power cycling. */
    if (pairing->store.status == ST_NV_EMPTY)
    {
        if (!link->idle || !link->idle(link->ctx) ||
            !st_pairing_provision(pairing, wire + 3,
                         pairing->role == ST_NV_ACQUISITION ? 1u : 0u, true))
            return 0;
        link->changed = true;
    }
    if (pairing->store.status != ST_NV_VALID ||
        memcmp(wire + 3, pairing->store.identity.peer_uid, 12))
        return 0;
    return wanted == AUTO_LINK_DISCOVER ? AutoLink_Encode(link, AUTO_LINK_ADVERTISE, wire + 3, out) : 0;
}
