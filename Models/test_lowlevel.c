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
typedef struct{uint16_t reg[64],response;uint8_t *rx;const uint8_t *tx;uint32_t hash;bool active,bad_crc,bad_read;unsigned waits,resets,transfers;} ads_bus;
static void reset(void *ctx,bool asserted){ads_bus *b=ctx;if(asserted){b->resets++;memset(b->reg,0,sizeof(b->reg));b->reg[0]=0x2201;b->reg[2]=0x0510;b->response=0xff22;}}
static st_async_result ads_submit(void *ctx,const uint8_t *tx,uint8_t *rx){ads_bus *b=ctx;C(!b->active);b->active=true;b->tx=tx;b->rx=rx;b->hash=st_crc32(tx,12);b->waits=1;b->transfers++;return ST_IO_DONE;}
static st_async_result ads_poll(void *ctx){ads_bus *b=ctx;C(b->active&&st_crc32(b->tx,12)==b->hash);if(b->waits--!=0)return ST_IO_WAIT;
 uint16_t command=(uint16_t)(((uint16_t)b->tx[0]<<8)|b->tx[1]),value=(uint16_t)(((uint16_t)b->tx[3]<<8)|b->tx[4]);
 memset(b->rx,0,12);b->rx[0]=(uint8_t)(b->response>>8);b->rx[1]=(uint8_t)b->response;
 uint16_t crc=st_crc16(b->rx,9);b->rx[9]=(uint8_t)(crc>>8);b->rx[10]=(uint8_t)(crc^(b->bad_crc?1:0));
 unsigned address=(command>>7)&63u;
 if((command&0xe000)==0x6000){b->reg[address]=value;b->response=(uint16_t)(0x4000|(address<<7));}
 else if((command&0xe000)==0xa000)b->response=(uint16_t)(b->reg[address]^((b->bad_read&&address==2)?1:0));
 else if(command==0x0555)b->response=0x0555;else b->response=0x8103;
 b->active=false;return ST_IO_DONE;
}
static void ads_tests(void){ads_bus b={0};st_ads a;st_ads_config cfg={8192000,2048,128,1,2,true};
 st_ads_init(&a,(st_ads_port){&b,reset,ads_submit,ads_poll},cfg,0);
 for(unsigned i=0;i<200&&!a.ready&&!a.failed;i++)st_ads_tick(&a,i*1000);
 C(a.ready&&!a.failed&&b.resets==1);C(b.reg[2]==0x0110&&b.reg[3]==0x0312&&b.reg[4]==7);
 C(b.transfers==19);G("ADS hardware-reset timing / full 24-bit frames / delayed command response / ID / three write ACKs and readbacks / LOCK / startup drain");
 memset(&b,0,sizeof(b));b.bad_read=true;st_ads_init(&a,(st_ads_port){&b,reset,ads_submit,ads_poll},cfg,UINT32_MAX-500);
 for(unsigned i=0;i<200&&!a.ready&&!a.failed;i++)st_ads_tick(&a,(UINT32_MAX-500)+i*1000);
 C(a.failed&&!a.ready);memset(&b,0,sizeof(b));b.bad_crc=true;st_ads_init(&a,(st_ads_port){&b,reset,ads_submit,ads_poll},cfg,0);
 for(unsigned i=0;i<200&&!a.failed;i++)st_ads_tick(&a,i*1000);
 C(a.failed);
 cfg.electrically_verified=false;st_ads_init(&a,(st_ads_port){&b,reset,ads_submit,ads_poll},cfg,0);C(a.failed);
 G("ADS CRC/readback rejection / timer wrap / unverified analog profile never ready");
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
