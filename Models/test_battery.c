#include "st_battery.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define C(x) do { checks++; if(!(x)) { fprintf(stderr,"battery line%d: %s\n",__LINE__,#x);exit(1); } } while(0)
#if ST_ACQ_BATTERY_PRESENT && ST_ACQ_BATTERY_VERIFIED
static uint16_t BatteryTest_Code(uint32_t mv)
{ return (uint16_t)((mv*4095u+3300u)/(2u*3300u)); }
#endif
unsigned st_test_battery(unsigned *groups)
{
    /* G071 PA3 circuit is independent from the F411 build-time circuit flags. */
    BatteryMonitor controller = {0};
    st_voltage controller_vdda = {3300, 0, true, true};
    Battery_UpdateCircuit(&controller, 2606, &controller_vdda, 0, true, true);
    C(controller.voltage.valid && controller.voltage.mv >= 4199 &&
      controller.voltage.mv <= 4202 && controller.voltage.mv != controller_vdda.mv);
    Battery_UpdateCircuit(&controller, 2606, &controller_vdda, 0, false, true);
    C(!controller.voltage.valid);
    Battery_UpdateCircuit(&controller, 2606, &controller_vdda, 0, true, false);
    C(!controller.voltage.valid);
    controller_vdda = (st_voltage){3000, 100000, true, true};
    Battery_UpdateCircuit(&controller, 2867, &controller_vdda, 100000, true, true);
    C(controller.voltage.valid && controller.voltage.mv >= 4199 &&
      controller.voltage.mv <= 4202);
    Battery_UpdateCircuit(&controller, 2867, &controller_vdda, 1100001, true, true);
    C(!controller.voltage.valid && !controller.initialized);
    Battery_UpdateCircuit(&controller, 4095, &controller_vdda, 100000, true, true);
    C(!controller.voltage.valid);
    Battery_UpdateCircuit(&controller, 0, &controller_vdda, 100000, true, true);
    C(!controller.voltage.valid);
    controller_vdda.valid = false;
    Battery_UpdateCircuit(&controller, 2867, &controller_vdda, 100000, true, true);
    C(!controller.voltage.valid);
    puts("PASS controller PA3 divider, independent circuit acceptance, VDDA compensation and stale/error rejection");
    (*groups)++;
    BatteryMonitor battery={0};
    st_voltage vdda={3300,0,true,true};
#if !ST_ACQ_BATTERY_PRESENT || !ST_ACQ_BATTERY_VERIFIED
    /* Every possible floating input must remain unknown despite a valid VDDA. */
    for (unsigned raw=0;raw<=4095;raw++)
    {
        Battery_Update(&battery,(uint16_t)raw,&vdda,0);
        C(!battery.voltage.valid);
        C(Battery_Evaluate(&battery,0).kind==ST_V_UNKNOWN);
        C(battery.view.led_mask==0&&!battery.initialized);
    }
    puts("PASS unconfirmed PC0: all4096 ADC codes invalid/UNKNOWN, LEDs OFF; VDDA cannot become battery");
    (*groups)++;
    return checks;
#else
    const uint32_t levels[]={4200,3750,3550,3430,3300};
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
    Battery_Update(&battery,BatteryTest_Code(ST_BATTERY_BLINK_MV+10u),&vdda,100000);
    (void)Battery_Evaluate(&battery,100000);C(battery.blinking);
    battery.initialized=false;
    Battery_Update(&battery,BatteryTest_Code(ST_BATTERY_BLINK_MV+50u),&vdda,200000);
    (void)Battery_Evaluate(&battery,200000);C(!battery.blinking);
    Battery_Invalidate(&battery);C(Battery_Evaluate(&battery,200000).led_mask==0);
    /* Independent rounded readings from the manufacturer's 25C/650mA curve.
     * Each nominal quarter has ~758mAh, final half-band ~379mAh to 3.2V. */
    const uint32_t thresholds[]={3840,3630,3480};
    const uint8_t lower_masks[]={7,3,1};
    for(unsigned i=0;i<3;i++)
    {
        battery=(BatteryMonitor){0};
        Battery_Update(&battery,BatteryTest_Code(thresholds[i]+50u),&vdda,0);
        C(Battery_Evaluate(&battery,0).led_mask==(uint8_t)((lower_masks[i]<<1)|1u));
        battery.initialized=false;
        Battery_Update(&battery,BatteryTest_Code(thresholds[i]-10u),&vdda,100000);
        C(Battery_Evaluate(&battery,100000).led_mask==(uint8_t)((lower_masks[i]<<1)|1u));
        battery.initialized=false;
        Battery_Update(&battery,BatteryTest_Code(thresholds[i]-50u),&vdda,200000);
        C(Battery_Evaluate(&battery,200000).led_mask==lower_masks[i]);
    }
    puts("PASS battery PC0 divide-by-two/VDDA compensation, 4/3/2/1 LEDs, lower half 1Hz blink, filter/hysteresis, stale/invalid OFF");
    (*groups)++;
    return checks;
#endif
}
