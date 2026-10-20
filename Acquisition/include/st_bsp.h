#ifndef SWIFTTHRUST_ACQUISITION_BSP_H
#define SWIFTTHRUST_ACQUISITION_BSP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Contract only. No HAL implementation, pins, clocks or output driver exists. */
#if defined(ST_TARGET_BUILD) || defined(__arm__) || defined(__thumb__) || defined(__ICCARM__) ||   \
    defined(__ARMCC_VERSION)
#error                                                                                             \
    "SwiftThrust board UNBOUND: confirm schematic, power, clocks, DMA and supply monitors before target integration"
#endif
#define ST_ADC_FRAME_BYTES 12u
#define ST_REQUESTED_SAMPLE_RATE_SPS 2000u
/* Called with the DRDY-edge timestamp, not SD/write/radio completion time.
 * DMA buffers remain owned by producer until the consumer explicitly releases.
 * A DRDY sequence advances even on DMA-busy/queue-full; never wait in the ISR.
 * Port must extend the hardware timer atomically through wrap and preserve gaps. */
typedef struct
{
    uint64_t drdy_time_us;
    uint32_t drdy_sequence;
    uint8_t frame[ST_ADC_FRAME_BYTES];
} st_adc_capture;
typedef enum
{
    ST_IO_OK,
    ST_IO_BUSY,
    ST_IO_FAILED,
    ST_IO_UNBOUND
} st_io_result;
typedef struct
{
    uint32_t millivolts;
    uint64_t sampled_at_us;
    bool sense_circuit_present;
    bool valid;
} st_supply_measurement;
/* Future implementations must be asynchronous/bounded and return UNBOUND
 * until the actual board is verified. CRC, conversion and file work stay out
 * of DRDY ISR. SD completion != durable file sync. Radio is drop-on-busy. */
st_io_result st_bsp_adc_submit_dma(uint32_t sequence, uint64_t drdy_time_us);
st_io_result st_bsp_storage_append(const uint8_t *data, size_t length);
st_io_result st_bsp_storage_sync(void);
st_io_result st_bsp_radio_status_try_send(const uint8_t *data, size_t length);
st_supply_measurement st_bsp_acquisition_supply(void);
/* No ignition/launch/output execution API is defined. */
#endif
