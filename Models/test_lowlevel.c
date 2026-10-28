#include "st_link2.h"
#include "st_ads.h"
#include "st_radio.h"
#include "st_ui.h"
#include "st_debug.h"
#include "sx128x_regs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,groups;
#define C(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL lowlevel:%d %s\n",__LINE__,#x);exit(1);}}while(0)
#define G(n) do{groups++;printf("PASS %s\n",n);}while(0)
typedef struct
{
    uint8_t reg[4], *rx;
    const uint8_t *tx;
    uint32_t hash, now_us, reset_us;
    size_t bytes;
    bool active, bad_read, spi_error, submit_error, stuck;
    unsigned waits, resets, starts, stops, transfers, commands[8];
} ads_bus;
static st_async_result ads_submit(void *ctx, const uint8_t *tx, uint8_t *rx, size_t bytes)
{
    ads_bus *b = ctx;
    if (b->submit_error)
        return ST_IO_ERROR;
    C(!b->active && bytes >= 1 && bytes <= ADS1220_TRANSFER_BYTES);
    if (b->resets)
        C((uint32_t)(b->now_us - b->reset_us) >= 58u);
    b->active = true;
    b->tx = tx;
    b->rx = rx;
    b->bytes = bytes;
    b->hash = st_crc32(tx, bytes);
    b->waits = 1;
    if (b->transfers < 8u)
        b->commands[b->transfers] = tx[0];
    ++b->transfers;
    return ST_IO_DONE;
}
static st_async_result ads_poll(void *ctx)
{
    ads_bus *b = ctx;
    C(b->active && st_crc32(b->tx, b->bytes) == b->hash);
    if (b->stuck || b->waits-- != 0)
        return ST_IO_WAIT;
    b->active = false;
    if (b->spi_error)
        return ST_IO_ERROR;
    memset(b->rx, 0, b->bytes);
    uint8_t command = b->tx[0];
    if (command == ADS1220_COMMAND_RESET)
    {
        C(b->bytes == 1);
        ++b->resets;
        b->reset_us = b->now_us;
        memset(b->reg, 0, sizeof(b->reg));
    }
    else if (command == (ADS1220_COMMAND_RREG | 3))
    {
        C(b->bytes == 5);
        memcpy(b->rx + 1, b->reg, 4);
        if (b->bad_read)
            b->rx[3] ^= 1;
    }
    else if (command == (ADS1220_COMMAND_WREG | 3))
    {
        C(b->bytes == 5);
        memcpy(b->reg, b->tx + 1, 4);
    }
    else if (command == ADS1220_COMMAND_START)
    {
        C(b->bytes == 1);
        ++b->starts;
    }
    else if (command == ADS1220_COMMAND_POWERDOWN)
    {
        C(b->bytes == 1);
        ++b->stops;
    }
    else
        C(false);
    return ST_IO_DONE;
}
static void AdsTest_Tick(st_ads *a, ads_bus *b, uint32_t base, unsigned count)
{
    for (unsigned i = 0; i < count && !a->ready && !a->failed && !a->powered_down; ++i)
    {
        b->now_us = base + i * 10u;
        st_ads_tick(a, b->now_us);
    }
}
static st_ads_config AdsTest_Config(void)
{
    return (st_ads_config){.clock_hz = 4096000u, .reference_uv = 5000000u, .avdd_uv = 5000000u,
        .gain = 1u, .data_rate = ADS1220_RATE_2000_TURBO, .operating_mode = ADS1220_MODE_TURBO,
        .pga_bypass = true, .continuous = true, .electrically_verified = true};
}
static void ads_tests(void)
{
    ads_bus b = {0};
    st_ads a;
    st_ads_config cfg = AdsTest_Config();
    st_ads_init(&a, (st_ads_port){&b, ads_submit, ads_poll}, cfg, 0);
    AdsTest_Tick(&a, &b, 0, 2000);
    C(a.ready && !a.failed && b.resets == 1 && b.starts == 1);
    const uint8_t expected[4] = {0x31, 0xd4, 0x40, 0x00};
    C(!memcmp(b.reg, expected, sizeof(expected)));
    C(b.transfers == 5 && b.commands[0] == 0x06 && b.commands[1] == 0x23 &&
      b.commands[2] == 0x43 && b.commands[3] == 0x08 && b.commands[4] == 0x23);
    C(Ads1220_GetConversionCycles(cfg) == 2072u);
    C(Ads1220_GetFirstConversionCycles(cfg) == 2186u);
    C(Ads1220_GetRateMilliHz(cfg) == 1976834u);
    cfg.continuous = false;
    C(Ads1220_GetConversionCycles(cfg) == 2130u);
    C(Ads1220_GetFirstConversionCycles(cfg) == 2130u);
    cfg = AdsTest_Config();
    C(Ads1220_Reinitialize(&a, b.now_us) == ADS1220_INVALID_STATE);
    uint32_t shutdown_us = b.now_us;
    C(Ads1220_RequestPowerdown(&a, shutdown_us) == ADS1220_OK);
    AdsTest_Tick(&a, &b, shutdown_us, 10);
    C(!a.ready && !a.powered_down && b.stops == 1);
    AdsTest_Tick(&a, &b, shutdown_us + 100u, 100);
    C(a.powered_down && !a.failed);
    C(Ads1220_Reinitialize(&a, b.now_us) == ADS1220_OK);
    AdsTest_Tick(&a, &b, b.now_us, 2000);
    C(a.ready && b.resets == 2 && b.starts == 2);
    G("ADS1220 RESET settle / 4-byte WREG-RREG / START / POWERDOWN drain / reinitialize / exact 2072-clock Turbo period");

    uint8_t encoded[4];
    for (unsigned gain = 1; gain <= 128; gain <<= 1)
    {
        cfg = AdsTest_Config();
        cfg.gain = (uint8_t)gain;
        cfg.pga_bypass = false;
        cfg.input_range_known = true;
        cfg.max_differential_uv = 10000u;
        cfg.common_mode_min_uv = cfg.common_mode_max_uv = 2500000u;
        C(Ads1220_EncodeConfig(cfg, encoded) == ADS1220_OK);
        C((encoded[0] & 0xf1u) == 0x30u);
        unsigned decoded_gain = 1u << ((encoded[0] >> 1) & 7u);
        C(decoded_gain == gain);
    }
    cfg.common_mode_min_uv = 1000000u;
    C(!st_ads_config_valid(cfg));
    cfg.common_mode_min_uv = cfg.common_mode_max_uv = 2500000u;
    cfg.max_differential_uv = 40000u;
    C(!st_ads_config_valid(cfg));
    cfg.max_differential_uv = 37000u;
    C(!st_ads_config_valid(cfg)); /* PGA clips before ADC fullscale with 5V reference. */
    cfg.max_differential_uv = 10000u;
    cfg.input_range_known = false;
    C(!st_ads_config_valid(cfg));
    cfg = AdsTest_Config(); cfg.gain = 8; C(!st_ads_config_valid(cfg));
    cfg = AdsTest_Config(); cfg.clock_hz = 8192000; C(!st_ads_config_valid(cfg));
    cfg = AdsTest_Config(); cfg.data_rate = 7; C(!st_ads_config_valid(cfg));
    cfg = AdsTest_Config(); cfg.reference_uv = 6000000; C(!st_ads_config_valid(cfg));
    cfg = AdsTest_Config(); cfg.electrically_verified = false; C(!st_ads_config_valid(cfg));
    C(Ads1220_EncodeConfig(AdsTest_Config(), NULL) == ADS1220_INVALID_CONFIG);
    G("ADS1220 gain encoding / unknown-sensor gain-one fallback / fullscale and PGA common-mode bounds / illegal configuration rejection");

    cfg = AdsTest_Config();
    memset(&b, 0, sizeof(b)); b.bad_read = true;
    st_ads_init(&a, (st_ads_port){&b, ads_submit, ads_poll}, cfg, UINT32_MAX - 500u);
    AdsTest_Tick(&a, &b, UINT32_MAX - 500u, 2000);
    C(a.failed && !a.ready && a.result == ADS1220_READBACK_ERROR);
    memset(&b, 0, sizeof(b));
    st_ads_init(&a, (st_ads_port){&b, ads_submit, ads_poll}, cfg, 0);
    while (a.phase < 4u && !a.failed)
    {
        b.now_us += 10u;
        st_ads_tick(&a, b.now_us);
    }
    b.bad_read = true;
    AdsTest_Tick(&a, &b, b.now_us, 2000);
    C(a.failed && a.result == ADS1220_READBACK_ERROR);
    memset(&b, 0, sizeof(b)); b.spi_error = true;
    st_ads_init(&a, (st_ads_port){&b, ads_submit, ads_poll}, cfg, 0);
    AdsTest_Tick(&a, &b, 0, 2000);
    C(a.failed && a.result == ADS1220_SPI_ERROR);
    memset(&b, 0, sizeof(b)); b.submit_error = true;
    st_ads_init(&a, (st_ads_port){&b, ads_submit, ads_poll}, cfg, 0);
    AdsTest_Tick(&a, &b, 0, 2000);
    C(a.failed && a.result == ADS1220_SPI_ERROR);
    memset(&b, 0, sizeof(b)); b.stuck = true;
    st_ads_init(&a, (st_ads_port){&b, ads_submit, ads_poll}, cfg, 0);
    AdsTest_Tick(&a, &b, 0, 2000);
    C(Ads1220_RequestPowerdown(&a, b.now_us) == ADS1220_BUSY);
    st_ads_tick(&a, 2000001u);
    C(a.failed && a.result == ADS1220_TIMEOUT && !a.ready);
    G("ADS1220 reset/config readback rejection / SPI submit-completion errors / bounded timeout / timer wrap");

    static const uint8_t raw[][3] = {{0,0,0},{0,0,1},{0xff,0xff,0xff},{0x7f,0xff,0xff},
        {0x80,0,0},{0x80,0,1},{0x7f,0xff,0xfe}};
    static const int32_t decoded[] = {0,1,-1,8388607,-8388608,-8388607,8388606};
    for (unsigned i = 0; i < sizeof(decoded) / sizeof(decoded[0]); ++i)
        C(Ads1220_DecodeRaw24(raw[i]) == decoded[i]);
    G("ADS1220 single signed24 zero / positive / negative / exact saturation boundaries");
}

typedef struct{bool active;unsigned pages,page,x,bytes;uint8_t pixels[1024];const uint8_t *p;uint8_t copy[32];size_t n;} sh_bus;
static st_async_result sh_submit(void *ctx,uint8_t address,const uint8_t *p,size_t n){sh_bus *b=ctx;C(address==0x3c&&!b->active);b->active=true;b->p=p;b->n=n;memcpy(b->copy,p,n);return ST_IO_DONE;}
static st_async_result sh_poll(void *ctx){sh_bus *b=ctx;C(b->active&&!memcmp(b->copy,b->p,b->n));b->active=false;
 if(b->copy[0]==0x40){C(b->n==17&&b->x+16<=128);memcpy(b->pixels+b->page*128+b->x,b->copy+1,16);b->x+=16;b->bytes+=16;}
 else if(b->n==4){C(b->copy[1]>=0xb0&&b->copy[1]<=0xb7&&b->copy[2]==2&&b->copy[3]==0x10);b->page=b->copy[1]-0xb0;b->pages++;b->x=0;}
 return ST_IO_DONE;
}
static void oled_test(void){sh_bus b={0};st_oled o;st_i2c_port p={&b,sh_submit,sh_poll};
 st_oled_init_profile(&o,p,(st_oled_config){ST_OLED_SH1106,0x3c,2},0);
 for(unsigned i=0;i<500&&o.phase!=5;i++)st_oled_service(&o,100+i);
 C(o.on&&!o.failed&&b.pages==8&&b.bytes==1024);
 uint8_t frame[1024];for(unsigned i=0;i<1024;i++)frame[i]=(uint8_t)(i^0x5a);
 C(st_oled_present(&o,frame));for(unsigned i=0;i<500&&o.phase!=5;i++)st_oled_service(&o,1000+i);
 C(b.pages==16&&b.bytes==2048&&!memcmp(b.pixels,frame,1024));
 st_oled_init_profile(&o,p,(st_oled_config){ST_OLED_SSD1306,0x3d,2},0);C(o.failed);
 G("SH1106 0x3C / eight page addresses / two-column offset / all 1024 bytes / SSD1306 profile separation");
}
typedef struct{uint8_t regs[65536],payload[ST2_MAX];uint16_t irq;uint8_t length;unsigned reset_count,commands;bool ready,fail,patch_seen;uint8_t last_write[ST2_MAX],last_length;} rf_bus;
static bool rf_ready(void *ctx){return ((rf_bus*)ctx)->ready;}
static bool rf_reset(void *ctx){rf_bus *b=ctx;b->reset_count++;b->ready=true;return true;}
static bool rf_command(void *ctx,const uint8_t *cmd,uint16_t cn,const uint8_t *tx,uint8_t *rx,uint16_t n){rf_bus *b=ctx;b->commands++;if(b->fail)return false;C(cn>0);
 if(cmd[0]==0x18){C(cn==3);unsigned a=((unsigned)cmd[1]<<8)|cmd[2];for(unsigned i=0;i<n;i++)b->regs[a+i]=tx[i];if(a==0x925)b->patch_seen=true;}
 if(rx){memset(rx,0,n);if(cmd[0]==0x19){unsigned a=((unsigned)cmd[1]<<8)|cmd[2];memcpy(rx,b->regs+a,n);}
 else if(cmd[0]==0x15){C(n==2);rx[0]=(uint8_t)(b->irq>>8);rx[1]=(uint8_t)b->irq;}
 else if(cmd[0]==0x17){C(n==2);rx[0]=b->length;rx[1]=128;}
 else if(cmd[0]==0x1b){C(cmd[1]==128);memcpy(rx,b->payload,n);}}
 if(cmd[0]==0x97){C(cn==3);b->irq&=(uint16_t)~((uint16_t)(((uint16_t)cmd[1]<<8)|cmd[2]));}
 if(cmd[0]==0x1a){C(cn==2&&n<=49);memcpy(b->last_write,tx,n);b->last_length=(uint8_t)n;}
 return true;
}
static void radio_test(void){static rf_bus b;memset(&b,0,sizeof(b));st_radio r;
 st_radio_config c={.frequency_hz=2473000000u,.power_dbm=12,.sync_word=0x12,.modulation={SX128X_LORA_RANGING_SF10,SX128X_LORA_RANGING_BW_800,SX128X_LORA_RANGING_CR_4_5},.tx_timeout_ms=500,.verified=true};
 st_radio_init(&r,(st_radio_port){&b,rf_command,rf_reset,rf_ready},c);
 for(unsigned i=0;i<20&&r.phase<13&&!r.failed;i++)st_radio_tick(&r,i);
 C(!r.failed&&r.phase==13&&b.reset_count==1&&b.patch_seen);
 C((b.regs[SX128X_REG_LNA_REGIME]&0xC0u)==0xC0u);
 st2_packet packet={.type=ST2_START,.epoch=123,.id=7};uint8_t bytes[ST2_MAX],received[ST2_MAX];size_t n=st2_encode(&packet,bytes),rn=0;
 C(st_radio_send(&r,bytes,n));st_radio_tick(&r,20);C(r.tx_active&&!r.tx_pending&&b.last_length==n&&!memcmp(bytes,b.last_write,n));
 b.irq=SX128X_IRQ_TX_DONE;st_radio_irq_isr(&r);st_radio_tick(&r,30);C(!r.tx_active&&r.tx_done==1);
 memcpy(b.payload,bytes,n);b.length=(uint8_t)n;b.irq=SX128X_IRQ_RX_DONE;st_radio_irq_isr(&r);st_radio_tick(&r,40);
 C(st_radio_receive(&r,received,&rn)&&rn==n&&!memcmp(bytes,received,n));
 b.irq=SX128X_IRQ_RX_DONE|SX128X_IRQ_CRC_ERROR;st_radio_irq_isr(&r);st_radio_tick(&r,50);C(!r.rx_ready&&r.rx_errors==1);
 C(st_radio_send(&r,bytes,n));st_radio_tick(&r,60);st_radio_tick(&r,700);C(!r.tx_active&&r.tx_timeouts==1);
 b.ready=false;st_radio_tick(&r,800);st_radio_tick(&r,901);C(r.failed);
 c.verified=false;st_radio_init(&r,(st_radio_port){&b,rf_command,rf_reset,rf_ready},c);C(r.failed);
 G("official SX128x LoRa init and SF erratum / reset despite BUSY / variable-length TX-RX / IRQ-CRC / timeout / BUSY latch / unverified profile (R8 framing)");
}
typedef struct{bool active;const uint8_t *p;uint8_t copy[64];size_t n;unsigned bytes;bool busy;} debug_bus;
static st_async_result debug_submit(void *ctx,const uint8_t *p,size_t n){debug_bus *b=ctx;if(b->busy)return ST_IO_WAIT;C(!b->active&&n<=64);b->active=true;b->p=p;b->n=n;memcpy(b->copy,p,n);return ST_IO_DONE;}
static st_async_result debug_poll(void *ctx){debug_bus *b=ctx;C(b->active&&!memcmp(b->copy,b->p,b->n));b->active=false;b->bytes+=(unsigned)b->n;return ST_IO_DONE;}
static void debug_test(void){debug_bus b={0};st_debug d;st_debug_init(&d,(st_debug_port){&b,debug_submit,debug_poll});uint8_t line[128];memset(line,'X',128);
 C(st_debug_write(&d,line,128,0));C(st_debug_write(&d,line,128,1));C(!st_debug_write(&d,line,1,2));
 b.busy=true;st_debug_service(&d);C(!d.inflight&&d.head-d.tail==256);b.busy=false;
 for(unsigned i=0;i<8;i++)st_debug_service(&d);
 C(b.bytes==256&&d.head==d.tail);
 C(st_debug_write(&d,line,128,1000));C(st_debug_write(&d,line,128,1001));
 C(st_debug_write(&d,line,128,2000));C(st_debug_write(&d,line,128,2001));C(!st_debug_write(&d,line,128,3000));
 G("debug UART foreground queue / nonblocking busy / retained TX bytes / 256B-per-second limit / overflow drop");
}
unsigned st_test_lowlevel(unsigned *out){ads_tests();oled_test();radio_test();debug_test();*out+=groups;return checks;}
