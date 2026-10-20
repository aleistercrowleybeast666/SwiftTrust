#include "st_ads.h"
#include <string.h>
static bool gain_valid(uint8_t g)
{
    return g && g <= 128 && (g & (g - 1u)) == 0;
}
bool st_ads_config_valid(st_ads_config c)
{
    if (!c.electrically_verified || !c.clock_hz || c.clock_hz > 8400000 || !gain_valid(c.gain0) ||
        !gain_valid(c.gain1) || c.power_mode > 2)
        return false;
    return c.osr == 64 || (c.osr >= 128 && c.osr <= 8192 && (c.osr & (c.osr - 1u)) == 0);
}
static unsigned gain_code(uint8_t g)
{
    unsigned n = 0;
    while (g > 1)
    {
        g >>= 1;
        n++;
    }
    return n;
}
static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static bool valid_frame(const uint8_t *p)
{
    return !p[2] && !p[11] && st_crc16(p, 9) == be16(p + 9);
}
void st_ads_init(st_ads *a, st_ads_port port, st_ads_config cfg, uint32_t now)
{
    memset(a, 0, sizeof(*a));
    a->port = port;
    a->config = cfg;
    a->started_us = now;
    if (!st_ads_config_valid(cfg) || !port.reset || !port.submit || !port.poll)
    {
        a->failed = true;
        return;
    }
    unsigned osr = cfg.osr, code = 0;
    while (osr > 128)
    {
        osr >>= 1;
        code++;
    }
    a->values[0] = 0x0110; /* 24-bit, CCITT output, input CRC off, RESET clear, timeout on */
    a->values[1] = (uint16_t)(0x0300 | (cfg.osr == 64 ? 0x20 : (code << 2)) | cfg.power_mode);
    a->values[2] = (uint16_t)((gain_code(cfg.gain1) << 4) | gain_code(cfg.gain0));
}
void st_ads_tick(st_ads *a, uint32_t now)
{
    if (a->failed || a->ready)
        return;
    if ((uint32_t)(now - a->started_us) > 2000000)
    {
        a->failed = true;
        return;
    }
    if (a->inflight)
    {
        st_async_result r = a->port.poll(a->port.ctx);
        if (r == ST_IO_WAIT)
            return;
        a->inflight = false;
        if (r == ST_IO_ERROR || !valid_frame(a->rx))
        {
            a->failed = true;
            return;
        }
        uint16_t response = be16(a->rx);
        unsigned p = a->phase;
        if ((p == 2 && response != 0xff22) || (p == 4 && (response & 0xff00) != 0x2200))
        {
            a->failed = true;
            return;
        }
        if (p >= 6 && p <= 10 && p % 2 == 0 &&
            response != (uint16_t)(0x4000 | ((2u + (p - 6) / 2) << 7)))
        {
            a->failed = true;
            return;
        }
        if (p >= 12 && p <= 16 && p % 2 == 0 && response != a->values[(p - 12) / 2])
        {
            a->failed = true;
            return;
        }
        if (p == 18 && response != 0x0555)
        {
            a->failed = true;
            return;
        }
        a->phase++;
        if (a->phase == 21)
        {
            a->ready = true;
            return;
        } /* two NULL frames drain startup FIFO */
    }
    if (a->phase == 0)
    {
        a->port.reset(a->port.ctx, true);
        uint64_t hold = (2048ull * 1000000 + a->config.clock_hz - 1) / a->config.clock_hz + 1;
        a->due_us = now + (uint32_t)(hold < 1000 ? 1000 : hold);
        a->phase = 1;
        return;
    }
    if (a->phase == 1)
    {
        if ((uint32_t)(now - a->due_us) >= 0x80000000u)
            return;
        a->port.reset(a->port.ctx, false);
        a->due_us = now + 500;
        a->phase = 2;
        return;
    }
    if (a->phase == 2 && (uint32_t)(now - a->due_us) >= 0x80000000u)
        return;
    uint16_t command = 0, value = 0;
    unsigned p = a->phase;
    if (p == 3)
        command = 0xa000; /* ID single register, response on next frame */
    else if (p >= 5 && p <= 9 && p % 2)
    {
        unsigned index = (p - 5) / 2;
        command = (uint16_t)(0x6000 | ((index + 2) << 7));
        value = a->values[index];
    }
    else if (p >= 11 && p <= 15 && p % 2)
        command = (uint16_t)(0xa000 | ((2u + (p - 11) / 2) << 7));
    else if (p == 17)
        command = 0x0555;
    memset(a->tx, 0, 12);
    a->tx[0] = (uint8_t)(command >> 8);
    a->tx[1] = (uint8_t)command;
    a->tx[3] = (uint8_t)(value >> 8);
    a->tx[4] = (uint8_t)value;
    st_async_result r = a->port.submit(a->port.ctx, a->tx, a->rx);
    if (r == ST_IO_DONE)
        a->inflight = true;
    else if (r == ST_IO_ERROR)
        a->failed = true;
}
