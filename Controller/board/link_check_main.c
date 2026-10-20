/* This target is an explicit component LINK CHECK, not operational firmware.
 * No GPIO, COM, SPI, SD or radio is initialized. Never flash to another project.
 * Full board entry/task scheduling, verified peripherals and NV backend remain pending. */
#include "st_core.h"
#include "st_radio.h"
#include "st_debug.h"
#include "st_nv.h"
#include "st_ui.h"
#include "st_cal_remote.h"
st_cal_remote st_linkcheck_cal_remote;
st_cal_ui st_linkcheck_cal_ui;
st_nv_store st_linkcheck_nv;
st_radio st_linkcheck_radio;
st_debug st_linkcheck_debug;
st_oled st_linkcheck_oled;
st2_client st_linkcheck_client;
void *volatile st_linkcheck_objects[] = {
    &st_linkcheck_cal_remote, &st_linkcheck_cal_ui, &st_linkcheck_nv,    &st_linkcheck_radio,
    &st_linkcheck_debug,      &st_linkcheck_oled,   &st_linkcheck_client};
volatile unsigned st_link_check_crc_ok;
void _init(void)
{
}
void _fini(void)
{
}
int main(void)
{
    const uint8_t test[] = "123456789";
    st_link_check_crc_ok = st_crc16(test, 9) == 0x29b1u && st_crc32(test, 9) == 0xcbf43926u;
    st_link_check_crc_ok &= st_linkcheck_objects[0] != 0;
    for (;;)
    {
        __asm volatile("wfi");
    }
}
