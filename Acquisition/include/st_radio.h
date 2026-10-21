#ifndef ST_RADIO_H
#define ST_RADIO_H
#include "st_core.h"
#include "sx128x.h"
typedef struct
{
    void *ctx;
    /* Bounded SPI command including BUSY checks. Called only in radio task, never ISR.
     * F4 port must allow ADC IRQ/task preemption while waiting. No unbounded loops. */
    bool (*command)(void *, const uint8_t *, uint16_t, const uint8_t *, uint8_t *, uint16_t);
    bool (*reset)(void *);
    bool (*ready)(void *);
} st_radio_port;
typedef struct
{
    uint32_t frequency_hz;
    int8_t power_dbm;
    uint8_t sync_word;
    sx128x_mod_params_lora_t modulation;
    uint16_t tx_timeout_ms;
    bool verified;
} st_radio_config;
typedef struct
{
    st_radio_port port;
    st_radio_config config;
    sx128x_pkt_params_lora_t packet;
    unsigned phase;
    bool failed, tx_pending, rx_ready, tx_active;
    _Atomic(bool) irq;
    uint8_t tx[ST_PROTOCOL_MAX_BYTES], rx[ST_PROTOCOL_MAX_BYTES], tx_length, rx_length;
    uint32_t tx_started_ms, last_poll_ms, busy_since_ms, tx_done, tx_timeouts, rx_errors,
        rx_dropped;
    bool busy_seen;
} st_radio;
bool st_radio_config_valid(st_radio_config);
void st_radio_init(st_radio *, st_radio_port, st_radio_config);
void st_radio_irq_isr(st_radio *);
void st_radio_tick(st_radio *, uint32_t now_ms);
/* One queued packet. STATUS can replace queued STATUS; commands/ACK are never overwritten. */
bool st_radio_send(st_radio *, const uint8_t *, size_t);
bool st_radio_receive(st_radio *, uint8_t out[ST_PROTOCOL_MAX_BYTES], size_t *n);
/* Exclusive radio owner only. Cancel exactly matching queued bytes, never an
 * on-air transmission. Used before tick when protocol epoch/request changes. */
bool st_radio_cancel_queued(st_radio *, const uint8_t *, size_t);
#endif
