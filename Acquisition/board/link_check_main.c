#if !defined(ST_REFERENCE_APPLICATION)
/* This target is an explicit component LINK CHECK, not operational firmware.
 * No GPIO, COM, SPI, SD or radio is initialized. Never flash to another project.
 * Full board entry/task scheduling, verified peripherals and NV backend remain pending. */
#include "st_core.h"
#include "st_radio.h"
#include "st_link_runtime.h"
st_link_runtime st_linkcheck_runtime;
#include "st_debug.h"
#include "st_nv.h"
#include "st_cal_service.h"
#include "st_block.h"
#include "st_cal_endpoint.h"
#include "st_cal_fatfs.h"
#include "st_cal_system.h"
#include "st_f4_capture.h"
st_cal_system st_linkcheck_cal_system;
st_fatfs_worker st_linkcheck_storage_worker;
st_f4_capture_port st_linkcheck_capture;
st2_server st_linkcheck_server;

st_nv_store st_linkcheck_nv;
st_block_device st_linkcheck_block;
st_radio st_linkcheck_radio;
st_debug st_linkcheck_debug;
st_logger st_linkcheck_logger;
void *volatile st_linkcheck_objects[] = {&st_linkcheck_runtime,        &st_linkcheck_cal_system,
                                         &st_linkcheck_storage_worker, &st_linkcheck_capture,
                                         &st_linkcheck_server,         &st_linkcheck_nv,
                                         &st_linkcheck_block,          &st_linkcheck_radio,
                                         &st_linkcheck_debug,          &st_linkcheck_logger};
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

#endif
