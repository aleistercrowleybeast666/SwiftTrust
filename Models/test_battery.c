#include "st_battery.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define C(x) do { checks++; if(!(x)) { fprintf(stderr,"battery line%d: %s\n",__LINE__,#x);exit(1); } } while(0)
static uint16_t BatteryTest_Code(uint32_t mv)
{ return (uint16_t)((mv*4095u+3300u)/(2u*3300u)); }
unsigned st_test_battery(unsigned *groups)
{
    BatteryMonitor battery={0};
    st_voltage vdda={3300,0,true,true};
    const uint32_t levels[]={4200,3800,3600,3400,3200};
    const uint8_t masks[]={15,7,3,1,1};
    for(unsigned i=0;i<5;i++)
    {
        battery=(BatteryMonitor){0};
        Battery_Update(&battery,BatteryTest_Code(levels[i]),&vdda,0);
        st_voltage_view view=Battery_Evaluate(&battery,0);
        C(battery.voltage.valid&&view.led_mask==masks[i]);
        C(view.mv+2>=levels[i]&&view.mv<=levels[i]+2);
    }
    C(battery.blinking);
    C(Battery_Evaluate(&battery,499999).led_mask==1);
    C(Battery_Evaluate(&battery,500000).led_mask==0);
    C(Battery_Evaluate(&battery,1000000).led_mask==1);
    C(Battery_Evaluate(&battery,1001000).kind==ST_V_STALE);
    C(!battery.blinking&&!battery.initialized);
    Battery_Update(&battery,0,&vdda,0);C(!battery.voltage.valid);
    Battery_Update(&battery,4095,&vdda,0);C(!battery.voltage.valid);
    Battery_Update(&battery,BatteryTest_Code(4500),&vdda,0);C(!battery.voltage.valid);
    Battery_Update(&battery,BatteryTest_Code(4200),&vdda,1000001);C(!battery.voltage.valid);
    vdda.valid=false;Battery_Update(&battery,2606,&vdda,0);C(!battery.voltage.valid);
    vdda=(st_voltage){3000,0,true,true};
    Battery_Update(&battery,2867,&vdda,0);C(battery.voltage.mv>=4199&&battery.voltage.mv<=4202);
    vdda=(st_voltage){3300,0,true,true};battery=(BatteryMonitor){0};
    Battery_Update(&battery,BatteryTest_Code(3800),&vdda,0);
    uint32_t before=battery.filtered_mv;
    Battery_Update(&battery,BatteryTest_Code(3600),&vdda,100000);
    C(battery.filtered_mv<before&&battery.filtered_mv>3600);
    battery=(BatteryMonitor){0};
    Battery_Update(&battery,BatteryTest_Code(3200),&vdda,0);
    C(Battery_Evaluate(&battery,0).led_mask==1&&battery.blinking);
    battery.initialized=false;
    Battery_Update(&battery,BatteryTest_Code(3310),&vdda,100000);
    (void)Battery_Evaluate(&battery,100000);C(battery.blinking);
    battery.initialized=false;
    Battery_Update(&battery,BatteryTest_Code(3350),&vdda,200000);
    (void)Battery_Evaluate(&battery,200000);C(!battery.blinking);
    Battery_Invalidate(&battery);C(Battery_Evaluate(&battery,200000).led_mask==0);
    puts("PASS battery PC0 divide-by-two/VDDA compensation, 4/3/2/1 LEDs, lower half 1Hz blink, filter/hysteresis, stale/invalid OFF");
    (*groups)++;
    return checks;
}
