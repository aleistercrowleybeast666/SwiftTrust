#include "st_link2.h"
#include "st_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,groups;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"R2 failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define G(s) do{groups++;puts("PASS " s);}while(0)
typedef struct {uint64_t counter,last;uint32_t epoch;unsigned writes;bool fail;} nv;
static bool counter(void *ctx,uint64_t *out){nv *v=ctx;if(v->fail||v->counter==UINT64_MAX)return false;*out=++v->counter;v->writes++;return true;}
static bool epoch(void *ctx,uint64_t n,uint32_t *out){nv *v=ctx;if(v->fail||n<=v->last||v->epoch==UINT32_MAX)return false;v->last=n;*out=++v->epoch;v->writes++;return true;}
static const uint8_t a[12]={1,2,3,4,5,6,7,8,9,10,11,12},b[12]={13,14,15,16,17,18,19,20,21,22,23,24};
static st2_timing timing(void){return (st2_timing){10000,2200,2500,100000,4};}
static st2_measurement measured(void){return (st2_measurement){ST2_TF_READY,true,true,false,7400};}
static size_t pack(st2_packet p,uint8_t out[35]){size_t n=st2_encode(&p,out);C(n>0);return n;}
static void codec(void){
 const uint8_t types[]={ST2_STATUS,ST2_START,ST2_STOP,ST2_ACK,ST2_DETAILS_QUERY,ST2_DETAILS_REPLY,ST2_HELLO,ST2_HELLO_REPLY,ST2_COMMIT};
 const size_t lengths[]={12,9,9,10,10,27,35,27,15};uint8_t out[35],bad[35];
 for(unsigned i=0;i<9;i++){
  st2_packet p={.type=types[i],.epoch=0x12345678,.id=345,.mv=7400,.counter=UINT64_C(123456789012),.count=16,.section=1,.fragment=0x21};
  if(p.type==ST2_STATUS)p.flags=0xf2;
  if(p.type==ST2_ACK)p.flags=(uint8_t)(ST2_COMPLETED|(ST_CAPTURING<<3)|(ST2_TF_OPEN<<6));
  memcpy(p.uid,a,12);memcpy(p.peer,b,12);memset(p.data,0xa5,16);size_t n=pack(p,out);C(n==lengths[i]);C(out[0]>=1&&out[0]<=9);
  st2_packet q;C(st2_decode(&q,out,n));C(q.type==p.type);
  for(size_t k=0;k<n;k++){memcpy(bad,out,n);bad[k]^=1;C(!st2_decode(&q,bad,n));}
  for(size_t k=0;k<9;k++)C(!st2_decode(&q,out,k));
 }
 st2_packet p={.type=ST2_STATUS,.epoch=1,.id=1,.flags=32,.mv=65535};C(!st2_encode(&p,out));
 p.mv=0;C(st2_encode(&p,out)==12);st2_packet q;C(st2_decode(&q,out,12)&&q.mv==0);
 out[0]=0xf1;st_put16(out+10,st_crc16(out,10));C(!st2_decode(&q,out,12));
 p.flags=7;C(!st2_encode(&p,out));p.type=ST2_DETAILS_REPLY;p.count=17;C(!st2_encode(&p,out));
 G("R2 exact lengths / every byte corruption / short frames / unknown voltage versus measured zero / reserved values");
}
static void connect(st2_client *c,st2_server *s,uint32_t now){
 uint8_t x[35],y[35];size_t n,m;C(st2_client_connect(c,now));C(st2_client_tx_due(c,now,x,&n)&&n==35);
 st2_client_tx_accepted(c);st2_client_tx_done(c,now+10,0);m=st2_server_receive(s,x,n,measured(),(uint64_t)now*1000,y);C(m==27);
 unsigned writes=((nv*)s->nv.ctx)->writes;uint32_t e=s->epoch;
 C(st2_server_receive(s,x,n,measured(),(uint64_t)now*1000,y)==27&&s->epoch==e&&((nv*)s->nv.ctx)->writes==writes);
 C(st2_client_receive(c,y,m,now+20));C(st2_client_tx_due(c,now+20,x,&n)&&n==15);
 st2_client_tx_accepted(c);st2_client_tx_done(c,now+30,0);m=st2_server_receive(s,x,n,measured(),(uint64_t)now*1000,y);C(m==12);
 C(st2_client_receive(c,y,m,now+40)&&c->link==ST2_LINK_UP&&st2_client_fresh(c,now+40));
}
static void sessions(void){nv cn={0},sn={0};st_logger l;st_logger_init(&l,(st_log_config){0},(st_file_port){0});
 st2_client c;st2_server s;st2_client_init(&c,(st2_nv_port){&cn,NULL,counter},timing(),b,a);
 st2_server_init(&s,&l,(st2_nv_port){&sn,epoch,NULL},timing(),a,b,0);connect(&c,&s,0);C(sn.writes==1&&cn.writes==1);
 uint8_t old[35],out[35];size_t n=pack((st2_packet){.type=ST2_START,.epoch=s.epoch,.id=1},old);
 st2_server reboot;st2_server_init(&reboot,&l,(st2_nv_port){&sn,epoch,NULL},timing(),a,b,sn.last);
 C(!st2_server_receive(&reboot,old,n,measured(),500000,out));
 st2_packet hello={.type=ST2_HELLO,.counter=1};memcpy(hello.uid,b,12);memcpy(hello.peer,a,12);n=pack(hello,old);
 C(!st2_server_receive(&reboot,old,n,measured(),500000,out));
 connect(&c,&reboot,1000);C(c.epoch==2);sn.fail=true;hello.counter=3;n=pack(hello,old);C(!st2_server_receive(&reboot,old,n,measured(),500000,out));
 cn.fail=true;C(!st2_client_connect(&c,2000));cn.fail=false;cn.counter=UINT64_MAX;C(!st2_client_connect(&c,2000));
 st2_client_init(&c,(st2_nv_port){0},timing(),b,a);C(!st2_client_connect(&c,2000));
 G("R2 paired UID handshake / durable reservation once / boot rejects old counter and epoch / renewal / persistence failure and exhaustion");
}
static void transactions(void){nv cn={0},sn={0};st_logger l;st_logger_init(&l,(st_log_config){0},(st_file_port){0});
 st2_client c;st2_server s;st2_client_init(&c,(st2_nv_port){&cn,NULL,counter},timing(),b,a);
 st2_server_init(&s,&l,(st2_nv_port){&sn,epoch,NULL},timing(),a,b,0);connect(&c,&s,0);
 /* Exercise transaction transport with real stop state machine, not a fake start backend. */
 l.state=ST_CAPTURING;c.flags=ST_CAPTURING;uint8_t cmd[35],ack[35],tmp[35];size_t n,m;
 C(st2_client_command(&c,ST2_STOP,100));C(st2_client_tx_due(&c,100,cmd,&n)&&n==9);
 st2_client_tx_accepted(&c);C(!st2_client_tx_due(&c,10000,tmp,&m));st2_client_tx_done(&c,200,250);
 m=st2_server_receive(&s,cmd,n,measured(),200000,ack);C(m==10&&l.state==ST_STOPPING);
 C(st2_client_receive(&c,ack,m,210)&&c.pending&&c.transaction==ST_TX_WAIT_STATE);
 C(!st2_client_tx_due(&c,2949,tmp,&m));C(st2_client_tx_due(&c,2950,tmp,&m)&&m==n&&!memcmp(cmd,tmp,n));
 l.state=ST_IDLE; /* model asynchronous storage completion; logger independently regression-tested */
 m=st2_server_poll(&s,measured(),3000000,ack);C(m==10);st2_packet p;C(st2_decode(&p,ack,m)&&(p.flags&7u)==ST2_COMPLETED);
 /* Drop terminal ACK; state status cannot complete the matching transaction. */
 st2_measurement stale=measured();stale.adc_fresh=false;
 m=st2_server_poll(&s,stale,3001000,ack);C(m==12);C(st2_client_receive(&c,ack,m,3001)&&c.pending);
 m=st2_server_receive(&s,cmd,n,measured(),3002000,ack);C(m==10);C(st2_client_receive(&c,ack,m,3002)&&!c.pending&&c.transaction==ST_TX_COMPLETE);
 st2_packet conflict={.type=ST2_START,.epoch=s.epoch,.id=1};m=pack(conflict,tmp);m=st2_server_receive(&s,tmp,m,measured(),4000000,ack);C(st2_decode(&p,ack,m)&&(p.flags&7u)==ST2_CONFLICT);
 conflict.type=ST2_STOP;conflict.id=2;l.state=ST_CAPTURING;m=pack(conflict,tmp);C(st2_server_receive(&s,tmp,m,measured(),4001000,ack)==10);
 l.state=ST_FAULT;m=st2_server_poll(&s,measured(),4002000,ack);C(st2_decode(&p,ack,m)&&(p.flags&7u)==ST2_FAILED);
 c.flags=ST_IDLE;c.next_command=65535;C(st2_client_command(&c,ST2_START,5000)&&!c.next_command);
 c.pending=false;C(!st2_client_command(&c,ST2_START,5000));
 G("R2 stop idempotence / terminal ACK loss and retry / status never completes command / conflicting ID / async failure / no transaction wrap / TX_DONE retry anchor");
}
static void scheduling(void){nv cn={0},sn={0};st_logger l;st_logger_init(&l,(st_log_config){0},(st_file_port){0});
 st2_client c;st2_server s;st2_client_init(&c,(st2_nv_port){&cn,NULL,counter},timing(),b,a);st2_server_init(&s,&l,(st2_nv_port){&sn,epoch,NULL},timing(),a,b,0);connect(&c,&s,0);
 uint8_t out[35];st2_measurement m=measured();C(!st2_server_poll(&s,m,9999000,out));C(st2_server_poll(&s,m,10000000,out)==12);
 C(st2_client_receive(&c,out,12,10010));C(!st2_client_receive(&c,out,12,10011));C(c.received_ms==10010);
 C(st2_client_fresh(&c,43210));C(!st2_client_fresh(&c,43211));
 m.voltage_fresh=false;C(st2_server_poll(&s,m,10001000,out)==12);m.quality_problem=true;C(st2_server_poll(&s,m,10002000,out)==12);
 m.quality_problem=false;C(!st2_server_poll(&s,m,10003000,out));C(s.quality_sticky);
 s.status_sequence=65535;C(!st2_server_poll(&s,m,20002000,out)&&!s.active);
 st2_client_init(&c,(st2_nv_port){&cn,NULL,counter},timing(),b,a);C(st2_client_connect(&c,UINT32_MAX-100));
 size_t n;C(st2_client_tx_due(&c,20,out,&n));C(!st2_client_tx_due(&c,100001,out,&n)&&c.link==ST2_LINK_DOWN);
 G("R2 10s heartbeat / freshness event / sticky quality / stale or duplicate status rejected / time wrap / status sequence exhaustion / bounded handshake timeout");
}

static void details_ui(void){nv cn={0},sn={0};st_logger l;st_logger_init(&l,(st_log_config){0},(st_file_port){0});
 st2_client c;st2_server s;st2_client_init(&c,(st2_nv_port){&cn,NULL,counter},timing(),b,a);st2_server_init(&s,&l,(st2_nv_port){&sn,epoch,NULL},timing(),a,b,0);connect(&c,&s,0);
 uint8_t query[35],first[35],second[35],repeat[35];st2_details d;size_t n=st2_details_begin(&d,c.epoch,17,query);C(n==10);
 l.have_sample=true;l.last_sequence=123;l.last_time_us=1000000;size_t f=st2_server_receive(&s,query,n,measured(),2000000,first);C(f==27);
 l.last_sequence=999;size_t g=st2_server_poll(&s,measured(),2001000,second);C(g==18);
 C(st2_details_receive(&d,second,g)&&!d.complete);C(st2_details_receive(&d,first,f)&&d.complete);
 C(st_get32(d.data+4)==123&&st_get16(d.data+20)==1000);C(st2_details_receive(&d,first,f));
 C(st2_server_receive(&s,query,n,measured(),9000000,repeat)==f&&!memcmp(first,repeat,f));
 st2_packet p;C(st2_decode(&p,first,f));p.data[0]^=1;f=pack(p,repeat);C(!st2_details_receive(&d,repeat,f));
 p.id=18;f=pack(p,repeat);C(!st2_details_receive(&d,repeat,f));
 st_ui u;st_ui_init(&u);u.selection=2;C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100));C(u.page==ST_UI_CAPTURE);
 C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==ST_UI_CONFIRM&&u.selection==1);
 C(!st_ui2_handle(&u,ST_UI_PREV,&c,100));C(st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&c.request.type==ST2_START);
 st_ui_model model;u.page=ST_UI_STATUS;st_ui2_render(&u,&c,(st_voltage_view){0},100,&model);
 C(strstr(model.rows[4],"READY/CLOSED")&&strstr(model.rows[5],"FRESH")&&strstr(model.rows[6],"BACK"));
 c.pending=false;
 u.page=ST_UI_STATUS;u.selection=0;
 C(!st_ui2_handle(&u,ST_UI_NEXT,&c,100)&&u.selection==1);
 st_ui2_render(&u,&c,(st_voltage_view){0},100,&model);C(strstr(model.rows[7],"> CONNECT"));
 C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&c.link==ST2_LINK_HELLO&&c.request.type==ST2_HELLO&&!c.pending);
 for(unsigned item=0;item<4;item++){
  st_ui_init(&u);u.selection=item;C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100));
  st_ui_page expected[]={ST_UI_STATUS,ST_UI_IGNITION_ONLY,ST_UI_CAPTURE,ST_UI_COMBINED};C(u.page==expected[item]);
  if(item==1||item==3){
   /* A live client makes accidental acquisition dispatch observable. */
   c.link=ST2_LINK_UP;c.have_status=true;c.received_ms=100;c.next_command=41;
   c.flags=(uint8_t)((item==1?ST_IDLE:ST_CAPTURING)|(ST2_TF_OPEN<<3));
   uint8_t client_before[sizeof(c)];memcpy(client_before,&c,sizeof(c));
   st_ui_page confirmation=item==1?ST_UI_IGNITION_CONFIRM:ST_UI_COMBINED_CONFIRM;
   st_ui2_render(&u,&c,(st_voltage_view){0},100,&model);
   C(strstr(model.rows[3],"> START N/A")&&strstr(model.rows[4],"STOP N/A")&&strstr(model.rows[6],"BACK"));
   C(strstr(model.rows[7],"IGNITION UNAVAILABLE"));
   for(unsigned j=0;j<12;j++)C(!st_ui2_handle(&u,ST_UI_NEXT,&c,100)&&!c.pending);
   C(u.selection==0);
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==confirmation&&u.selection==1);
   st_ui2_render(&u,&c,(st_voltage_view){0},100,&model);C(strstr(model.rows[4],"> BACK"));
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==expected[item]&&u.selection==0);
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==confirmation);
   C(!st_ui2_handle(&u,ST_UI_BACK,&c,100)&&u.page==expected[item]&&u.selection==0);
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==confirmation);
   C(!st_ui2_handle(&u,ST_UI_PREV,&c,100)&&u.selection==0);
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==expected[item]&&u.unavailable);
   st_ui2_render(&u,&c,(st_voltage_view){0},100,&model);C(strstr(model.rows[7],"NOT IMPLEMENTED"));
   C(!st_ui2_handle(&u,ST_UI_NEXT,&c,100)&&u.selection==1);
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==expected[item]&&u.unavailable);
   C(!st_ui2_handle(&u,ST_UI_NEXT,&c,100)&&u.selection==2);
   st_ui2_render(&u,&c,(st_voltage_view){0},100,&model);C(strstr(model.rows[6],"> BACK"));
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==ST_UI_HOME&&u.selection==item&&!c.pending);
   C(!st_ui2_handle(&u,ST_UI_PRESS,&c,100)&&u.page==expected[item]);
   C(!st_ui2_handle(&u,ST_UI_BACK,&c,100)&&u.page==ST_UI_HOME&&u.selection==item);
   C(!memcmp(client_before,&c,sizeof(c)));
  }else C(!st_ui2_handle(&u,ST_UI_BACK,&c,100)&&u.page==ST_UI_HOME&&u.selection==item);
 }

 G("R2 diagnostic snapshot / request isolation / mode START-STOP-BACK / safe confirmation parents / unavailable ignition leaves live RF client unchanged");
}
unsigned st_test_r2(unsigned *out){codec();sessions();transactions();scheduling();details_ui();*out+=groups;return checks;}
