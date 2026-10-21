#include "st_radio.h"
#include "st_link2.h"
#include "sx128x_hal.h"
#include <string.h>
bool st_radio_config_valid(st_radio_config c)
{
    unsigned sf = (unsigned)c.modulation.sf, bw = (unsigned)c.modulation.bw,
             cr = (unsigned)c.modulation.cr;
    if (!c.verified || c.frequency_hz < 2400000000u || c.frequency_hz > 2500000000u ||
        c.power_dbm > 12 || c.power_dbm < -18 || sf < 0x50 || sf > 0xc0 || (sf & 15u) || cr < 1 ||
        cr > 4 || c.tx_timeout_ms < 50 || c.tx_timeout_ms > 5000)
        return false;
    return bw == 0x34 || bw == 0x26 || bw == 0x18 || bw == 0x0a;
}
void st_radio_init(st_radio *r, st_radio_port p, st_radio_config c)
{
    memset(r, 0, sizeof(*r));
    atomic_init(&r->irq, false);
    r->port = p;
    r->config = c;
    r->packet = (sx128x_pkt_params_lora_t){.preamble_len = {2, 3},
                                           .header_type = SX128X_LORA_RANGING_PKT_EXPLICIT,
                                           .pld_len_in_bytes = ST2_MAX,
                                           .crc_is_on = ST_RADIO_CRC_ENABLED != 0,
                                           .invert_iq_is_on = false};
    r->failed = !st_radio_config_valid(c) || !p.command || !p.reset || !p.ready;
    if (!r->failed &&
        sx128x_get_lora_time_on_air_in_ms(&r->packet, &c.modulation) + 25u > c.tx_timeout_ms)
        r->failed = true;
}
sx128x_hal_status_t sx128x_hal_wakeup(const void *ctx)
{
    const st_radio *r = ctx;
    /* This owner never enters SLEEP; reset starts awake. No unowned chip sleep API. */
    return r->port.ready(r->port.ctx) ? SX128X_HAL_STATUS_OK : SX128X_HAL_STATUS_ERROR;
}
sx128x_hal_status_t sx128x_hal_reset(const void *ctx)
{
    const st_radio *r = ctx;
    return r->port.reset(r->port.ctx) ? SX128X_HAL_STATUS_OK : SX128X_HAL_STATUS_ERROR;
}
sx128x_hal_status_t sx128x_hal_write(const void *ctx, const uint8_t *cmd, uint16_t cn,
                                     const uint8_t *data, uint16_t n)
{
    const st_radio *r = ctx;
    return r->port.command(r->port.ctx, cmd, cn, data, NULL, n) ? SX128X_HAL_STATUS_OK
                                                                : SX128X_HAL_STATUS_ERROR;
}
sx128x_hal_status_t sx128x_hal_read(const void *ctx, const uint8_t *cmd, uint16_t cn, uint8_t *data,
                                    uint16_t n)
{
    const st_radio *r = ctx;
    return r->port.command(r->port.ctx, cmd, cn, NULL, data, n) ? SX128X_HAL_STATUS_OK
                                                                : SX128X_HAL_STATUS_ERROR;
}
/* Armv6-M has no exclusive byte exchange. This bounded critical section
 * preserves PRIMASK and takes only the IRQ flag. No libatomic dependency. */
static bool take_irq(st_radio *r)
{
#if defined(__ARM_ARCH_6M__)
    uint32_t mask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(mask)::"memory");
    bool pending = atomic_load_explicit(&r->irq, memory_order_relaxed);
    atomic_store_explicit(&r->irq, false, memory_order_relaxed);
    __asm volatile("dmb\n msr primask, %0" ::"r"(mask) : "memory");
    return pending;
#else
    return atomic_exchange_explicit(&r->irq, false, memory_order_acq_rel);
#endif
}
void st_radio_irq_isr(st_radio *r)
{
    atomic_store_explicit(&r->irq, true, memory_order_release);
}
bool st_radio_send(st_radio *r, const uint8_t *data, size_t n)
{
    st2_packet p;
    if (r->failed || !st2_decode(&p, data, n))
        return false;
    if (r->tx_pending)
    {
        st2_packet old;
        if (!st2_decode(&old, r->tx, r->tx_length) || old.type != ST2_STATUS ||
            (p.type != ST2_STATUS && p.type != ST2_ACK && p.type != ST2_CAL_REPLY &&
             p.type != ST2_CAL_COMMAND && p.type != ST2_CAL_QUERY))
            return false;
    }
    memcpy(r->tx, data, n);
    r->tx_length = (uint8_t)n;
    r->tx_pending = true;
    return true;
}
bool st_radio_cancel_queued(st_radio *r, const uint8_t *p, size_t n)
{
    if (!r || !p || !r->tx_pending || n > sizeof(r->tx) || n != r->tx_length || memcmp(r->tx, p, n))
        return false;
    r->tx_pending = false;
    return true;
}
bool st_radio_receive(st_radio *r, uint8_t out[ST2_MAX], size_t *n)
{
    if (!r->rx_ready)
        return false;
    memcpy(out, r->rx, r->rx_length);
    *n = r->rx_length;
    r->rx_ready = false;
    return true;
}
static sx128x_status_t begin_rx(st_radio *r)
{
    r->packet.pld_len_in_bytes = ST2_MAX;
    sx128x_status_t s = sx128x_set_lora_pkt_params(r, &r->packet);
    return s == SX128X_STATUS_OK ? sx128x_set_rx(r, SX128X_TICK_SIZE_1000_US, 65535) : s;
}
void st_radio_tick(st_radio *r, uint32_t now)
{
    if (r->failed)
        return;
    if (r->phase == 0)
    {
        if (sx128x_hal_reset(r) != SX128X_HAL_STATUS_OK)
            r->failed = true;
        else
            r->phase = 1;
        return;
    }
    if (!r->port.ready(r->port.ctx))
    {
        if (!r->busy_seen)
        {
            r->busy_seen = true;
            r->busy_since_ms = now;
        }
        else if ((uint32_t)(now - r->busy_since_ms) > 100)
            r->failed = true;
        return;
    }
    r->busy_seen = false;
    sx128x_status_t s = SX128X_STATUS_OK;
    if (r->phase < 13)
    {
        switch (r->phase)
        {
        case 0:
            s = sx128x_hal_reset(r) == SX128X_HAL_STATUS_OK ? SX128X_STATUS_OK
                                                            : SX128X_STATUS_ERROR;
            break;
        case 1:
            s = sx128x_set_standby(r, SX128X_STANDBY_CFG_RC);
            break;
        case 2:
            s = sx128x_set_reg_mode(r, SX128X_REG_MODE_LDO);
            break;
        case 3:
            s = sx128x_set_pkt_type(r, SX128X_PKT_TYPE_LORA);
            if (s == SX128X_STATUS_OK)
                s = sx128x_set_lna_settings(r, SX128X_LNA_HIGH_SENSITIVITY_MODE);
            break;
        case 4:
            s = sx128x_set_rf_freq(r, r->config.frequency_hz);
            break;
        case 5:
            s = sx128x_set_lora_mod_params(r, &r->config.modulation);
            break;
        case 6:
            s = sx128x_set_lora_sync_word(r, r->config.sync_word);
            break;
        case 7:
            s = sx128x_set_lora_pkt_params(r, &r->packet);
            break;
        case 8:
            s = sx128x_set_buffer_base_address(r, 0, 128);
            break;
        case 9:
            s = sx128x_set_tx_params(r, r->config.power_dbm, SX128X_RAMP_20_US);
            break;
        case 10:
        {
            uint16_t mask = SX128X_IRQ_TX_DONE | SX128X_IRQ_RX_DONE | SX128X_IRQ_CRC_ERROR |
                            SX128X_IRQ_HEADER_ERROR | SX128X_IRQ_TIMEOUT;
            s = sx128x_set_dio_irq_params(r, mask, mask, 0, 0);
            break;
        }
        case 11:
            s = sx128x_clear_irq_status(r, SX128X_IRQ_ALL);
            break;
        default:
            s = begin_rx(r);
            break;
        }
        if (s != SX128X_STATUS_OK)
            r->failed = true;
        else
            r->phase++;
        return;
    }
    if (take_irq(r) || (uint32_t)(now - r->last_poll_ms) >= 100)
    {
        r->last_poll_ms = now;
        sx128x_irq_mask_t irq = 0;
        s = sx128x_get_and_clear_irq_status(r, &irq);
        if (s != SX128X_STATUS_OK)
        {
            r->failed = true;
            return;
        }
        if (irq & SX128X_IRQ_TX_DONE)
        {
            r->tx_active = false;
            r->tx_done++;
            s = begin_rx(r);
        }
        else if (irq & SX128X_IRQ_TIMEOUT)
        {
            if (r->tx_active)
                r->tx_timeouts++;
            r->tx_active = false;
            s = begin_rx(r);
        }
        if (irq & (SX128X_IRQ_CRC_ERROR | SX128X_IRQ_HEADER_ERROR))
            r->rx_errors++;
        else if (irq & SX128X_IRQ_RX_DONE)
        {
            sx128x_rx_buffer_status_t status;
            s = sx128x_get_rx_buffer_status(r, &status);
            if (s == SX128X_STATUS_OK && status.pld_len_in_bytes >= 9 &&
                status.pld_len_in_bytes <= ST2_MAX)
            {
                uint8_t data[ST2_MAX];
                s = sx128x_read_buffer(r, status.buffer_start_pointer, data,
                                       status.pld_len_in_bytes);
                st2_packet packet;
                if (s == SX128X_STATUS_OK && st2_decode(&packet, data, status.pld_len_in_bytes))
                {
                    if (r->rx_ready)
                        r->rx_dropped++;
                    else
                    {
                        memcpy(r->rx, data, status.pld_len_in_bytes);
                        r->rx_length = status.pld_len_in_bytes;
                        r->rx_ready = true;
                    }
                }
                else
                    r->rx_errors++;
            }
            else if (s == SX128X_STATUS_OK)
                r->rx_errors++;
        }
        if (s != SX128X_STATUS_OK)
        {
            r->failed = true;
            return;
        }
    }
    if (r->tx_active)
    {
        if ((uint32_t)(now - r->tx_started_ms) > (uint32_t)r->config.tx_timeout_ms + 100)
        {
            r->tx_active = false;
            r->tx_timeouts++;
            if (sx128x_set_standby(r, SX128X_STANDBY_CFG_RC) != SX128X_STATUS_OK ||
                begin_rx(r) != SX128X_STATUS_OK)
                r->failed = true;
        }
        return;
    }
    if (r->tx_pending)
    {
        s = sx128x_set_standby(r, SX128X_STANDBY_CFG_RC);
        if (s == SX128X_STATUS_OK)
            s = sx128x_write_buffer(r, 0, r->tx, r->tx_length);
        r->packet.pld_len_in_bytes = r->tx_length;
        if (s == SX128X_STATUS_OK)
            s = sx128x_set_lora_pkt_params(r, &r->packet);
        if (s == SX128X_STATUS_OK)
            s = sx128x_clear_irq_status(r, SX128X_IRQ_ALL);
        if (s == SX128X_STATUS_OK)
            s = sx128x_set_tx(r, SX128X_TICK_SIZE_1000_US, r->config.tx_timeout_ms);
        if (s == SX128X_STATUS_OK)
        {
            r->tx_pending = false;
            r->tx_active = true;
            r->tx_started_ms = now;
        }
        else
            r->failed = true;
    }
}
