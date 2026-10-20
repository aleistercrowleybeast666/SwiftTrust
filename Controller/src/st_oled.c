#include "st_ui.h"
#include <string.h>
/* SSD1306 Rev1.1 command table, internal charge pump appendix.
 * Horizontal addressing: columns 0..127, pages 0..7; no SH1106 column offset. */
static const uint8_t init_commands[] = {0x00, 0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
                                        0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12, 0x81,
                                        0x7f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0x2e};
static const uint8_t sh1106_commands[] = {0,    0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0,
                                          0x40, 0xad, 0x8b, 0xa1, 0xc8, 0xda, 0x12, 0x81,
                                          0x7f, 0xd9, 0x22, 0xdb, 0x40, 0xa4, 0xa6};
void st_oled_init(st_oled *o, st_i2c_port port, uint32_t now)
{
    st_oled_init_profile(o, port, (st_oled_config){ST_OLED_SSD1306, 0x3c, 0}, now);
}
void st_oled_init_profile(st_oled *o, st_i2c_port port, st_oled_config config, uint32_t now)
{
    memset(o, 0, sizeof(*o));
    o->port = port;
    o->power_on_ms = now;
    o->config = config;
    if (config.kind > ST_OLED_SH1106 || (config.address != 0x3c && config.address != 0x3d) ||
        config.column_offset > 4 || (config.kind == ST_OLED_SSD1306 && config.column_offset != 0))
        o->failed = true;
    if (!port.submit || !port.poll)
        o->failed = true;
}
bool st_oled_present(st_oled *o, const uint8_t frame[1024])
{
    if (o->failed || o->phase != 5 || o->inflight)
        return false;
    memcpy(o->frame, frame, 1024);
    o->offset = 0;
    o->phase = 2;
    return true;
}
void st_oled_service(st_oled *o, uint32_t now)
{
    if (o->failed)
        return;
    if (o->inflight)
    {
        st_async_result r = o->port.poll(o->port.ctx);
        if (r == ST_IO_ERROR)
        {
            o->failed = true;
            return;
        }
        if (r == ST_IO_WAIT)
            return;
        o->inflight = false;
        if (o->phase == 1)
            o->phase = 2;
        else if (o->phase == 2)
            o->phase = 3;
        else if (o->phase == 3)
        {
            o->offset += 16;
            if (o->offset == 1024)
                o->phase = o->on ? 5 : 4;
            else if (o->config.kind == ST_OLED_SH1106 && o->offset % 128 == 0)
                o->phase = 2;
        }
        else if (o->phase == 4)
        {
            o->on = true;
            o->phase = 5;
        }
    }
    if (o->phase == 0)
    {
        if ((uint32_t)(now - o->power_on_ms) < 100)
            return;
        o->phase = 1;
    }
    size_t n = 0;
    if (o->phase == 1)
    {
        const uint8_t *p = o->config.kind == ST_OLED_SH1106 ? sh1106_commands : init_commands;
        n = o->config.kind == ST_OLED_SH1106 ? sizeof(sh1106_commands) : sizeof(init_commands);
        memcpy(o->tx, p, n);
    }
    else if (o->phase == 2)
    {
        const uint8_t window[] = {0, 0x21, 0, 127, 0x22, 0, 7};
        if (o->config.kind == ST_OLED_SH1106)
        {
            o->tx[0] = 0;
            o->tx[1] = (uint8_t)(0xb0 + o->offset / 128);
            o->tx[2] = (uint8_t)(o->config.column_offset & 15u);
            o->tx[3] = (uint8_t)(0x10 | (o->config.column_offset >> 4));
            n = 4;
        }
        else
        {
            n = sizeof(window);
            memcpy(o->tx, window, n);
        }
    }
    else if (o->phase == 3)
    {
        o->tx[0] = 0x40;
        memcpy(o->tx + 1, o->frame + o->offset, 16);
        n = 17;
    }
    else if (o->phase == 4)
    {
        o->tx[0] = 0;
        o->tx[1] = 0xaf;
        n = 2;
    }
    if (!n)
        return;
    st_async_result r = o->port.submit(o->port.ctx, o->config.address, o->tx, n);
    if (r == ST_IO_ERROR)
        o->failed = true;
    else if (r == ST_IO_DONE)
        o->inflight = true;
}
