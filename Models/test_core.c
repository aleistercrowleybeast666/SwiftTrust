#include "st_core.h"
#include "st_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,groups;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
#define GROUP(name) do {groups++;printf("PASS %s\n",name);} while(0)
static st_capture capture(uint32_t seq) {
    st_capture c={.time_us=(uint64_t)seq*500,.sequence=seq,.bytes={1,3,0,0,1,0,0xff,0xff,0x00,0,0,0}};
    uint16_t crc=st_crc16(c.bytes,9);c.bytes[9]=(uint8_t)(crc>>8);c.bytes[10]=(uint8_t)crc;return c;
}
static void test_crc_adc(void) {
    const uint8_t *vector=(const uint8_t *)"123456789";
    CHECK(st_crc16(vector,9)==0x29b1);CHECK(st_crc32(vector,9)==0xcbf43926u);
    st_capture c=capture(1);st_adc_sample s=st_adc_decode(c.bytes);
    CHECK(s.flags==0&&s.ch0==256&&s.ch1==-256&&s.status==0x0103);
    c.bytes[3]=0x80;c.bytes[4]=c.bytes[5]=0;
    c.bytes[6]=0x7f;c.bytes[7]=c.bytes[8]=0xff;
    uint16_t crc=st_crc16(c.bytes,9);c.bytes[9]=(uint8_t)(crc>>8);c.bytes[10]=(uint8_t)crc;
    s=st_adc_decode(c.bytes);CHECK(s.ch0==-8388608&&s.ch1==8388607&&s.flags==ST_REC_SATURATED);
    c.bytes[4]^=1;s=st_adc_decode(c.bytes);CHECK(s.flags&ST_REC_CRC);
    c.bytes[0]=0x0b;c.bytes[2]=1;s=st_adc_decode(c.bytes);CHECK(s.flags&ST_REC_FORMAT);
    c.bytes[0]=0x75;s=st_adc_decode(c.bytes);CHECK(s.flags&ST_REC_ADC_FAULT);
    uint8_t record[32];st_record(record,&c,ST_REC_GAP,4);
    CHECK(st_get32(record)==1&&st_get64(record+4)==500&&st_get32(record+24)==4);
    CHECK(st_crc32(record,28)==st_get32(record+28));GROUP("CRC vectors / ADS signed24 / bad CRC-status-padding / record CRC");
}
static void test_ring(void) {
    st_ring q;st_ring_init(&q);st_capture c;
    for(unsigned i=0;i<ST_RING_CAPACITY;i++) {c=capture(i);CHECK(st_ring_push(&q,&c));}
    CHECK(!st_ring_push(&q,&c)&&atomic_load(&q.dropped)==1&&st_ring_count(&q)==1024);
    for(unsigned i=0;i<ST_RING_CAPACITY;i++) {CHECK(st_ring_pop(&q,&c));CHECK(c.sequence==i);}
    CHECK(!st_ring_pop(&q,&c));atomic_store(&q.head,UINT32_MAX-2);atomic_store(&q.tail,UINT32_MAX-2);
    for(unsigned i=0;i<8;i++) {c=capture(i);CHECK(st_ring_push(&q,&c));CHECK(st_ring_pop(&q,&c)&&c.sequence==i);}
    CHECK(st_ring_count(&q)==0);GROUP("SPSC FIFO / full-drop-new / 32-bit index wrap");
}
static void test_voltage(void) {
    st_voltage measured=st_voltage_from_adc(2048,3300,2,1,1000,true,true);
    CHECK(measured.valid&&measured.mv==3301&&measured.time_us==1000);
    CHECK(!st_voltage_from_adc(2048,3300,2,1,1000,true,false).valid);
    CHECK(!st_voltage_from_adc(5000,3300,2,1,1000,true,true).valid);
    CHECK(st_voltage_from_adc(0,3300,2,1,1000,true,true).valid);
    st_voltage_policy p={{3300,3550,3800,4050},20,3000,true};
    st_voltage v={3900,1000000,true,true};st_voltage_view old={0};
    CHECK(st_voltage_policy_valid(&p));old=st_voltage_evaluate(&p,&v,1000000,old);
    CHECK(old.kind==ST_V_MEASURED&&old.tier==3&&old.led_mask==7);
    v.mv=3790;old=st_voltage_evaluate(&p,&v,1000000,old);CHECK(old.tier==3);
    v.mv=3779;old=st_voltage_evaluate(&p,&v,1000000,old);CHECK(old.tier==2);
    v.mv=3810;old=st_voltage_evaluate(&p,&v,1000000,old);CHECK(old.tier==2);
    v.mv=3820;old=st_voltage_evaluate(&p,&v,1000000,old);CHECK(old.tier==3);
    old=st_voltage_evaluate(&p,&v,5000000,old);CHECK(old.kind==ST_V_STALE&&old.led_mask==0&&old.mv==3820);
    v.valid=false;old=st_voltage_evaluate(&p,&v,5000000,old);CHECK(old.kind==ST_V_UNKNOWN&&old.led_mask==0);
    v.valid=true;v.mv=0;old=st_voltage_evaluate(&p,&v,1000000,old);CHECK(old.kind==ST_V_LOW&&old.mv==0);
    p.bound=false;v.mv=4100;old=st_voltage_evaluate(&p,&v,1000000,old);CHECK(old.kind==ST_V_MEASURED&&old.led_mask==0);
    p.bound=true;p.threshold_mv[2]=3000;CHECK(!st_voltage_policy_valid(&p));
    GROUP("voltage missing/stale/real-zero / four tiers / hysteresis / unbound policy");
}
typedef struct {
    uint8_t file[200000];size_t length;const uint8_t *buffer;size_t n;uint32_t hash;
    st_file_op op;unsigned delay,left,opens,writes,syncs,closes,busy_submits;
    bool active,short_write,poll_error,submit_error;
} disk;
static st_async_result disk_submit(void *ctx,st_file_op op,const uint8_t *p,size_t n) {
    disk *d=ctx;CHECK(!d->active);
    if(d->submit_error) return ST_IO_ERROR;
    if(d->busy_submits) {d->busy_submits--;return ST_IO_WAIT;}
    d->active=true;d->op=op;d->buffer=p;d->n=n;d->left=d->delay;d->hash=n?st_crc32(p,n):0;
    if(op==ST_FILE_OPEN_NEW) d->opens++;
    if(op==ST_FILE_WRITE) d->writes++;
    if(op==ST_FILE_SYNC) d->syncs++;
    if(op==ST_FILE_CLOSE) d->closes++;
    return ST_IO_DONE;
}
static st_async_result disk_poll(void *ctx,size_t *bytes) {
    disk *d=ctx;CHECK(d->active);CHECK(!d->n||st_crc32(d->buffer,d->n)==d->hash);
    if(d->poll_error) return ST_IO_ERROR;
    if(d->left) {d->left--;return ST_IO_WAIT;}
    d->active=false;*bytes=0;
    if(d->op==ST_FILE_WRITE) {
        size_t n=d->short_write?d->n-1:d->n;CHECK(d->length+n<=sizeof(d->file));
        memcpy(d->file+d->length,d->buffer,n);d->length+=n;*bytes=n;
    }
    return ST_IO_DONE;
}
static st_log_config config(void) {
    return (st_log_config){.adc_clock_hz=8192000,.osr=2048,.rate_millihz=2000000,.boot_epoch=7,
        .gain_ch0=128,.gain_ch1=1,.simulated=true,.clock_verified=true};
}
static void init_logger(st_logger *l,disk *d) {
    memset(d,0,sizeof(*d));st_logger_init(l,config(),(st_file_port){d,disk_submit,disk_poll});
}
static void run_to(st_logger *l,st_capture_state target,uint64_t *now) {
    for(unsigned i=0;i<10000&&l->state!=target&&l->state!=ST_FAULT;i++) {st_logger_service(l,*now);*now+=1000;}
    CHECK(l->state==target);
}
static void verify_file(disk *d,unsigned samples,bool gaps) {
    CHECK(d->length==512+(samples+1u)*32u);CHECK(!memcmp(d->file,"STLOG001",8));
    CHECK(st_crc32(d->file,508)==st_get32(d->file+508));
    uint32_t prev=0;bool gap=false;
    for(unsigned i=0;i<samples;i++) {
        const uint8_t *p=d->file+512+i*32;
        CHECK(st_crc32(p,28)==st_get32(p+28));CHECK(!(st_get16(p+22)&ST_REC_END));
        if(i) CHECK(st_seq_newer(st_get32(p),prev));
        prev=st_get32(p);
        if(st_get16(p+22)&ST_REC_GAP) gap=true;
    }
    const uint8_t *end=d->file+d->length-32;
    CHECK(st_get16(end+22)==ST_REC_END&&st_crc32(end,28)==st_get32(end+28));
    CHECK(gap==gaps);
}
static void test_logger(const char *fixture) {
    static st_logger l;static disk d;uint64_t now=0;
    init_logger(&l,&d);l.config.clock_verified=false;CHECK(!st_logger_start(&l,now));
    l.config=config();CHECK(st_logger_start(&l,now));CHECK(l.state==ST_STARTING);
    st_capture c=capture(1);CHECK(!st_logger_offer_isr(&l,&c));run_to(&l,ST_CAPTURING,&now);
    CHECK(d.opens==1&&d.syncs==1&&!l.have_durable);
    d.delay=12;d.busy_submits=2;
    for(unsigned i=1;i<=1500;i++) {
        c=capture(i);CHECK(st_logger_offer_isr(&l,&c));st_logger_service(&l,now);now+=500;
    }
    CHECK(st_logger_stop(&l));CHECK(l.state==ST_STOPPING);CHECK(!st_logger_offer_isr(&l,&c));
    run_to(&l,ST_IDLE,&now);verify_file(&d,1500,false);
    CHECK(d.opens==1&&d.closes==1&&l.have_durable&&l.durable_sequence==1500);
    FILE *f=fopen(fixture,"wb");CHECK(f!=NULL);CHECK(fwrite(d.file,1,d.length,f)==d.length);CHECK(fclose(f)==0);
    GROUP("async open/header-sync/capture/drain/footer-sync/close / two retained staging buffers / durable sequence");
    init_logger(&l,&d);now=0;CHECK(st_logger_start(&l,now));run_to(&l,ST_CAPTURING,&now);
    c=capture(3);CHECK(st_logger_offer_isr(&l,&c));c=capture(8);CHECK(st_logger_offer_isr(&l,&c));
    c=capture(9);c.time_us=1;CHECK(st_logger_offer_isr(&l,&c));c.bytes[5]^=1;c.sequence=10;c.time_us=6000;
    CHECK(st_logger_offer_isr(&l,&c));CHECK(st_logger_stop(&l));run_to(&l,ST_IDLE,&now);verify_file(&d,4,true);
    CHECK(st_get32(d.file+512+32+24)==4);CHECK(st_get16(d.file+512+64+22)&ST_REC_TIME);
    CHECK(l.adc_errors==1);st_logger_source_error_isr(&l);CHECK(st_logger_error_count(&l)==2);GROUP("sequence-gap count / nonmonotonic timestamp / corrupt ADC preserved and flagged");
    init_logger(&l,&d);CHECK(st_logger_start(&l,now));run_to(&l,ST_CAPTURING,&now);d.delay=2000;
    unsigned accepted=0;for(unsigned i=1;i<=3000;i++) {
        c=capture(i);if(st_logger_offer_isr(&l,&c)) accepted++;st_logger_service(&l,now);now+=500;
    }
    CHECK(atomic_load(&l.ring.dropped)>0);CHECK(st_logger_stop(&l));d.delay=0;d.left=0;
    run_to(&l,ST_IDLE,&now);verify_file(&d,accepted,true);
    CHECK(st_get32(d.file+d.length-32+12)==3000-accepted);GROUP("slow-card backlog / bounded SRAM / exact overflow count / complete footer ordering");
    init_logger(&l,&d);CHECK(st_logger_start(&l,now));st_logger_service(&l,now);d.short_write=true;
    for(unsigned i=0;i<10&&l.state!=ST_FAULT;i++) st_logger_service(&l,now++);
    CHECK(l.state==ST_FAULT&&!atomic_load(&l.capture_enabled)&&!l.have_durable);
    CHECK(!st_logger_start(&l,now));GROUP("short write latches fault / no false durable / no automatic overwrite restart");
    init_logger(&l,&d);CHECK(st_logger_start(&l,now));d.submit_error=true;st_logger_service(&l,now);CHECK(l.state==ST_FAULT);
    init_logger(&l,&d);CHECK(st_logger_start(&l,now));st_logger_service(&l,now);d.poll_error=true;
    st_logger_service(&l,now);CHECK(l.state==ST_FAULT);GROUP("submit and asynchronous storage errors");
    init_logger(&l,&d);CHECK(st_logger_start(&l,now));CHECK(st_logger_stop(&l));run_to(&l,ST_IDLE,&now);
    verify_file(&d,0,false);CHECK(!l.have_durable);
    uint32_t generation=l.generation;CHECK(st_logger_start(&l,now)&&l.generation==generation+1);
    CHECK(st_logger_stop(&l));run_to(&l,ST_IDLE,&now);GROUP("STOP during STARTING / empty file closes / no capture enable");
}
static size_t query(st_server *s,uint8_t *out,uint64_t now) {
    st_packet p={.type=ST_MSG_QUERY,.session=s->session,.sequence=900};uint8_t in[49];
    size_t n=st_packet_encode(&p,in);
    return st_server_handle(s,in,n,(st_status){.mv=3910,.voltage_age_ms=20,.voltage_flags=3},now,out);
}
static void test_protocol_ui(const char *images) {
    static st_logger l;static disk d;init_logger(&l,&d);st_server s;st_server_init(&s,&l,123,7);
    st_client c;st_client_init(&c,123,1000,3);uint8_t in[49],out[49],saved[49];size_t n;
    n=query(&s,out,0);CHECK(n==49);CHECK(st_client_receive(&c,out,n,10));
    CHECK(c.known&&c.boot_epoch==7&&c.next_command==1);CHECK(st_client_voltage_age(&c,1010)==1020);
    memcpy(saved,out,n);CHECK(!st_client_receive(&c,saved,n,1000));CHECK(c.received_ms==10);
    st_ui ui;st_ui_init(&ui);ui.selection=2;CHECK(!st_ui_handle(&ui,ST_UI_PRESS,&c,10));
    CHECK(ui.page==ST_UI_CAPTURE&&!c.pending);CHECK(!st_ui_handle(&ui,ST_UI_PRESS,&c,10));
    CHECK(ui.page==ST_UI_CONFIRM&&ui.selection==1&&!c.pending);
    CHECK(!st_ui_handle(&ui,ST_UI_PRESS,&c,10)&&!c.pending); /* default BACK */
    CHECK(!st_ui_handle(&ui,ST_UI_PRESS,&c,10));CHECK(!st_ui_handle(&ui,ST_UI_PREV,&c,10));
    CHECK(st_ui_handle(&ui,ST_UI_PRESS,&c,10)&&c.pending);
    CHECK(st_client_tx_due(&c,10,in,&n)&&n==19);memcpy(saved,in,n);st_client_tx_sent(&c,10);
    size_t reply=st_server_handle(&s,in,n,(st_status){0},10000,out);CHECK(reply==21&&l.state==ST_STARTING);
    CHECK(st_client_receive(&c,out,reply,20)&&c.transaction==ST_TX_WAIT_STATE);
    CHECK(c.status.state==ST_IDLE&&c.received_ms==10&&st_client_voltage_age(&c,1010)==1020);
    st_packet ack;CHECK(st_packet_decode(&ack,out,reply)&&ack.payload[4]==ST_ACK_ACCEPTED);
    uint8_t ack_saved[49];memcpy(ack_saved,out,reply);
    CHECK(st_server_handle(&s,saved,n,(st_status){0},20000,out)==reply&&!memcmp(out,ack_saved,reply));
    (void)query(&s,out,30000);
    CHECK(st_server_handle(&s,saved,n,(st_status){0},40000,out)==reply&&!memcmp(out,ack_saved,reply));
    st_packet changed;CHECK(st_packet_decode(&changed,saved,n));changed.type=ST_MSG_STOP;n=st_packet_encode(&changed,in);
    reply=st_server_handle(&s,in,n,(st_status){0},50000,out);CHECK(st_packet_decode(&ack,out,reply));
    CHECK(ack.payload[4]==ST_ACK_CONFLICT&&l.state==ST_STARTING);
    uint64_t now=60000;run_to(&l,ST_CAPTURING,&now);n=query(&s,out,now);
    CHECK(st_client_receive(&c,out,n,100)&&c.transaction==ST_TX_COMPLETE&&!c.pending&&c.status.state==ST_CAPTURING);
    GROUP("wire sizes / duplicate-idempotence survives QUERY / conflicting ID / ACK != actual state / stale status replay");
    CHECK(st_client_command(&c,ST_MSG_STOP,110));CHECK(st_client_tx_due(&c,110,in,&n));st_client_tx_sent(&c,110);
    reply=st_server_handle(&s,in,n,(st_status){0},now,out);CHECK(l.state==ST_STOPPING); /* deliberately lose ACK */
    run_to(&l,ST_IDLE,&now);n=query(&s,out,now);CHECK(st_client_receive(&c,out,n,150));
    CHECK(c.transaction==ST_TX_COMPLETE&&!c.pending&&c.status.mv==3910);
    CHECK(!st_client_command(&c,ST_MSG_START,4000));
    GROUP("lost ACK recovered by fresh actual STATUS / stale-link start blocked / voltage preserved after STOP");
    n=query(&s,out,now);CHECK(st_client_receive(&c,out,n,4001));
    CHECK(st_client_command(&c,ST_MSG_START,4002));CHECK(st_client_tx_due(&c,4002,saved,&n));
    st_client_tx_sent(&c,4002);size_t n2;
    CHECK(!st_client_tx_due(&c,4500,in,&n2));CHECK(st_client_tx_due(&c,5002,in,&n2));CHECK(n2==n&&!memcmp(in,saved,n));
    st_client_tx_sent(&c,5002);CHECK(st_client_tx_due(&c,6002,in,&n2));st_client_tx_sent(&c,6002);
    CHECK(!st_client_tx_due(&c,7002,in,&n2));CHECK(!st_client_tx_due(&c,9002,in,&n2)&&c.transaction==ST_TX_TIMEOUT);
    GROUP("immutable retries / retry interval and attempt budget / bounded transaction timeout");
    n=query(&s,out,now);CHECK(st_client_receive(&c,out,n,9100));CHECK(st_client_command(&c,ST_MSG_START,9101));
    CHECK(st_client_tx_due(&c,9101,saved,&n));s.boot_epoch=8;s.status_sequence=0;
    uint8_t old_status[49];size_t status_n=query(&s,old_status,now);
    CHECK(st_client_receive(&c,old_status,status_n,9200)&&c.transaction==ST_TX_REBOOT&&!c.pending);
    reply=st_server_handle(&s,saved,n,(st_status){0},now,out);CHECK(st_packet_decode(&ack,out,reply));
    CHECK(ack.payload[4]==ST_ACK_BOOT&&l.state==ST_IDLE);
    st_packet old={.type=ST_MSG_STATUS,.length=34,.session=123,.sequence=999};st_status prior=c.status;
    prior.boot_epoch=7;st_status_pack(old.payload,&prior);n=st_packet_encode(&old,in);
    CHECK(!st_client_receive(&c,in,n,9300)&&c.boot_epoch==8);GROUP("new boot cancels pending / old-boot command and old STATUS rejected");
    unsigned opens=d.opens;
    st_packet probe={.type=ST_MSG_START,.length=4,.session=123,.sequence=s.last_command+1};
    st_put32(probe.payload,s.boot_epoch);n=st_packet_encode(&probe,in);CHECK(n==19);
    for(size_t i=0;i<n;i++) {uint8_t corrupt[49];memcpy(corrupt,in,n);corrupt[i]^=1;
        CHECK(st_server_handle(&s,corrupt,n,(st_status){0},now,out)==0);}
    for(size_t i=0;i<n;i++) CHECK(st_server_handle(&s,in,i,(st_status){0},now,out)==0);
    st_packet unknown={.type=0x55,.session=123};CHECK(st_packet_encode(&unknown,out)==0);
    CHECK(d.opens==opens&&l.state==ST_IDLE);GROUP("mutated/truncated packets and unknown type never change acquisition");
    st_ui_init(&ui);(void)st_ui_handle(&ui,ST_UI_PREV,&c,9200);CHECK(ui.selection==3);
    (void)st_ui_handle(&ui,ST_UI_PRESS,&c,9200);CHECK(ui.page==ST_UI_COMBINED&&!c.pending);
    CHECK(!st_ui_handle(&ui,ST_UI_PRESS,&c,9200)&&ui.page==ST_UI_HOME&&!c.pending);
    CHECK(ui.selection==3);
    st_ui_model model;uint8_t frame[1024];FILE *f=fopen(images,"wb");CHECK(f!=NULL);
    st_voltage_view own={.kind=ST_V_MEASURED,.mv=4100};
    st_ui_page pages[]={ST_UI_HOME,ST_UI_CAPTURE,ST_UI_STATUS,ST_UI_COMBINED};
    for(unsigned i=0;i<4;i++) {ui.page=pages[i];ui.selection=0;ui.unavailable=false;
        c.transaction=i==1?ST_TX_WAIT_STATE:ST_TX_NONE;
        c.status.state=i==1?ST_STARTING:(i==2?ST_CAPTURING:ST_IDLE);
        c.status.storage=i==2?1:0;ui.unavailable=false;
        st2_client active={.link=ST2_LINK_UP,.have_status=true,.received_ms=9200,.mv=c.status.mv,
            .flags=(uint8_t)(c.status.state|(ST2_TF_OPEN<<3)|32u|64u),.transaction=c.transaction,
            .timing={10000,2200,2500,100000,4}};
        st_ui2_render(&ui,&active,own,9200,&model);st_ui_raster(&model,frame);
        CHECK(strlen(model.rows[0])<=21);CHECK(!strcmp(model.rows[1],"SELF 4.10V"));
        CHECK(frame[0]!=0&&frame[127]==0);CHECK(fwrite(frame,1,1024,f)==1024);
    }
    CHECK(fclose(f)==0);ui.page=ST_UI_STATUS;st_ui_render(&ui,&c,own,14000,&model);
    CHECK(strstr(model.rows[0],"LOST")&&strstr(model.rows[2],"OLD"));
    GROUP("menu rotate/select/back / safe-default confirmation / static reserved pages / rendered 128x64 framebuffers");
}
typedef struct { bool active,error;unsigned wait,count,data_bytes,windows;const uint8_t *p;
    size_t n;uint8_t copy[32],pixels[1024]; } i2c;
static st_async_result i2c_submit(void *ctx,uint8_t address,const uint8_t *p,size_t n) {
    i2c *b=ctx;CHECK(address==0x3c&&!b->active&&n<=32);b->p=p;b->n=n;memcpy(b->copy,p,n);
    b->active=true;b->wait=1;b->count++;return ST_IO_DONE;
}
static st_async_result i2c_poll(void *ctx) {
    i2c *b=ctx;CHECK(b->active&&!memcmp(b->p,b->copy,b->n));if(b->error) return ST_IO_ERROR;
    if(b->wait) {b->wait--;return ST_IO_WAIT;}b->active=false;
    if(b->copy[0]==0x40) {CHECK(b->n==17&&b->data_bytes+16<=1024);
        memcpy(b->pixels+b->data_bytes,b->copy+1,16);b->data_bytes+=16;}
    else if(b->n==7&&b->copy[1]==0x21) {CHECK(b->copy[2]==0&&b->copy[3]==127&&b->copy[4]==0x22&&b->copy[5]==0&&b->copy[6]==7);
        b->windows++;b->data_bytes=0;}
    return ST_IO_DONE;
}
static void test_oled_input(void) {
    i2c b={0};st_oled o;st_oled_init(&o,(st_i2c_port){&b,i2c_submit,i2c_poll},0);
    st_oled_service(&o,99);CHECK(b.count==0);st_oled_service(&o,100);CHECK(b.copy[1]==0xae);
    for(unsigned i=0;i<500&&o.phase!=5;i++) st_oled_service(&o,100+i);
    CHECK(o.phase==5&&o.on&&b.data_bytes==1024&&b.windows==1);
    uint8_t frame[1024];for(unsigned i=0;i<1024;i++) frame[i]=(uint8_t)i;
    CHECK(st_oled_present(&o,frame));CHECK(!st_oled_present(&o,frame));
    for(unsigned i=0;i<500&&o.phase!=5;i++) st_oled_service(&o,1000+i);
    CHECK(o.phase==5&&b.windows==2&&!memcmp(frame,b.pixels,1024));
    CHECK(st_oled_present(&o,frame));st_oled_service(&o,2000);b.error=true;st_oled_service(&o,2001);CHECK(o.failed);
    GROUP("SSD1306 init/display-on-after-clear / 0x3C / address window / 16-byte chunks / retained buffer / bus fault");
    st_encoder e={0};CHECK(st_encoder_update(&e,65534,4)==0);CHECK(st_encoder_update(&e,2,4)==1);
    CHECK(st_encoder_update(&e,65534,4)==-1);CHECK(st_encoder_update(&e,65535,4)==0);
    CHECK(st_encoder_update(&e,2,4)==1);CHECK(st_encoder_update(&e,100,0)==0);
    st_button k={0};CHECK(!st_button_press(&k,true,0,20));CHECK(!st_button_press(&k,true,100,20));
    CHECK(!st_button_press(&k,false,101,20));CHECK(!st_button_press(&k,false,121,20));
    CHECK(!st_button_press(&k,true,122,20));CHECK(!st_button_press(&k,false,125,20));
    CHECK(!st_button_press(&k,true,130,20));CHECK(!st_button_press(&k,true,149,20));
    CHECK(st_button_press(&k,true,150,20));CHECK(!st_button_press(&k,true,200,20));
    GROUP("encoder wrap/detents / button bounce / held-at-boot suppression / one event per press");
}
unsigned st_test_lowlevel(unsigned *groups_out);
unsigned st_test_r2(unsigned *groups_out);
unsigned st_test_stop(unsigned *groups_out);
unsigned st_test_storage(unsigned *groups_out);
unsigned st_test_calibration(unsigned *groups_out,const char *images);
unsigned st_test_cal_integration(unsigned *groups_out);
int main(int argc,char **argv) {
    CHECK(argc==4);test_crc_adc();test_ring();test_voltage();test_logger(argv[1]);
    test_protocol_ui(argv[2]);test_oled_input();checks+=st_test_lowlevel(&groups);checks+=st_test_r2(&groups);checks+=st_test_stop(&groups);checks+=st_test_storage(&groups);checks+=st_test_cal_integration(&groups);checks+=st_test_calibration(&groups,argv[3]);
    printf("PASS total: %u groups, %u assertions; host sizeof(logger)=%zu, sizeof(oled)=%zu\n",groups,checks,sizeof(st_logger),sizeof(st_oled));
    return 0;
}
