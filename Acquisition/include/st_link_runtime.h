#ifndef ST_LINK_RUNTIME_H
#define ST_LINK_RUNTIME_H
#include "st_link2.h"
#include "st_auto_link.h"
#include "st_radio.h"
typedef struct
{
    st_radio *radio;
    AutoLink *auto_link;
    uint32_t observed_done, observed_timeouts;
    uint8_t packet[ST2_MAX], length, role;
    bool awaiting_tx, fault, ignition_tx;
    bool status_ack_due;
    uint32_t status_ack_sequence;
} st_link_runtime;
/* One foreground owner of radio tick/send/receive and this protocol endpoint.
 * Caller has initialized/verified radio GPIO/SPI/config and live HAL tick.
 * No pins, clocks, power, frequency, task priorities or NV backend are chosen.
 * Init only with no queued/active radio TX. Radio DIO ISR only raises its flag.
 * Each step: one driver tick, <=one RX and <=one send acceptance; no loops.
 * Server retains one produced reply until accepted, including all cal fragments.
 * Server step must share the serialized calibration/FatFs owner; logger ring
 * service and blocking storage worker still need their documented scheduling. */
bool st_link_runtime_init(st_link_runtime *, st_radio *);
void st_link_server_step(st_link_runtime *, st2_server *, st2_measurement, uint64_t now_us);
/* Explicit connect and UI commands remain caller actions. Runtime never creates
 * START/STOP or reconnects automatically. Actual TX_DONE/timeout anchors retry.
 * A superseded/expired queued command is removed BEFORE the next driver tick;
 * an already-on-air command cannot be recalled and remains uncertain. */
void st_link_client_step(st_link_runtime *, st2_client *, uint32_t now_ms, uint32_t jitter_ms);
#endif
