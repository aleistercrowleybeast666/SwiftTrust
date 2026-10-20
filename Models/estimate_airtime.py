"""Airtime only: compile and call the unmodified official Semtech calculation."""
from pathlib import Path
import subprocess,tempfile,hashlib,json,datetime,sys
sys.dont_write_bytecode=True
from local_tools import executable
R=Path(__file__).resolve().parents[1]
V=R/'Acquisition/vendor/semtech_sx128x'
C=r"""
#include "sx128x.h"
#include "sx128x_hal.h"
#include <stdio.h>
/* No physical HAL exists in this arithmetic harness. Every I/O call errors. */
sx128x_hal_status_t sx128x_hal_write(const void *a,const uint8_t *b,uint16_t c,const uint8_t *d,uint16_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return SX128X_HAL_STATUS_ERROR;}
sx128x_hal_status_t sx128x_hal_read(const void *a,const uint8_t *b,uint16_t c,uint8_t *d,uint16_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return SX128X_HAL_STATUS_ERROR;}
sx128x_hal_status_t sx128x_hal_reset(const void *a){(void)a;return SX128X_HAL_STATUS_ERROR;}
sx128x_hal_status_t sx128x_hal_wakeup(const void *a){(void)a;return SX128X_HAL_STATUS_ERROR;}
int main(void){
 sx128x_mod_params_lora_t profiles[]={
 {SX128X_LORA_RANGING_SF10,SX128X_LORA_RANGING_BW_800,SX128X_LORA_RANGING_CR_4_5},
 {SX128X_LORA_RANGING_SF11,SX128X_LORA_RANGING_BW_400,SX128X_LORA_RANGING_CR_4_5},
 {SX128X_LORA_RANGING_SF12,SX128X_LORA_RANGING_BW_200,SX128X_LORA_RANGING_CR_4_5},
 {SX128X_LORA_RANGING_SF12,SX128X_LORA_RANGING_BW_200,SX128X_LORA_RANGING_CR_4_8}};
 unsigned lengths[]={9,10,12,23,26,49};
 for(unsigned i=0;i<4;i++) for(unsigned j=0;j<6;j++) {
  sx128x_pkt_params_lora_t packet={.preamble_len={2,3},.header_type=SX128X_LORA_RANGING_PKT_EXPLICIT,
   .pld_len_in_bytes=(uint8_t)lengths[j],.crc_is_on=true,.invert_iq_is_on=false};
  unsigned ms=sx128x_get_lora_time_on_air_in_ms(&packet,&profiles[i]);
  printf("%u,%lu,%u,%u,%u\n",(unsigned)profiles[i].sf>>4,(unsigned long)sx128x_get_lora_bw_in_hz(profiles[i].bw),
    (unsigned)profiles[i].cr+4,lengths[j],ms);
 }
}
"""
def main():
 with tempfile.TemporaryDirectory(prefix='swift-airtime-') as d:
  t=Path(d);source=t/'airtime.c';source.write_text(C,encoding='utf-8');exe=t/'airtime.exe'
  subprocess.run([str(executable('HOST_CC','gcc')),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(V),str(source),str(V/'sx128x.c'),'-o',str(exe)],check=True,capture_output=True)
  result=subprocess.check_output([str(exe)],text=True)
 rows=[]
 for line in result.splitlines():
  sf,bw,cr,payload,ms=map(int,line.split(','));rows.append(dict(sf=sf,bw_hz=bw,cr=f'4/{cr}',payload_bytes=payload,airtime_ms_ceil=ms))
 report={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'preamble_symbols':16,'explicit_header':True,'phy_crc':True,
  'source_sha256':hashlib.sha256((V/'sx128x.c').read_bytes()).hexdigest(),'estimates':rows,
  'excludes':['BUSY','SPI','RX/TX switching','guard time','ACK','retries','collision/backoff'],'hardware_measured':False}
 (R/'Models/airtime_estimates.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(result);print('Semtech source SHA256',report['source_sha256'])
if __name__=='__main__':main()
