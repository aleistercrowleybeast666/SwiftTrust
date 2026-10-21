#ifndef __ST_AUTO_LINK_H
#define __ST_AUTO_LINK_H
#include "st_pairing.h"
enum { AUTO_LINK_DISCOVER = 0x0F, AUTO_LINK_ADVERTISE = 0x10, AUTO_LINK_BYTES = 33 };
typedef struct
{
    st_pairing *pairing;
    void *ctx;
    bool (*idle)(void *);
    uint8_t key[16];
    uint16_t network;
    uint32_t last_send_ms;
    bool sent, changed;
} AutoLink;
void AutoLink_Init(AutoLink *, st_pairing *, const uint8_t key[16], uint16_t network,
                   void *, bool (*idle)(void *));
bool AutoLink_TxDue(AutoLink *, uint32_t now_ms, uint8_t out[35], size_t *);
size_t AutoLink_Receive(AutoLink *, const uint8_t *, size_t, uint8_t out[35]);
bool AutoLink_TakeChanged(AutoLink *);
#endif
