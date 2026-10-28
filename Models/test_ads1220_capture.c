#include "st_ads_hal.h"
#include "st_f4_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define C(value) do { ++checks; if (!(value)) { fprintf(stderr, "FAIL ADS capture:%d %s\n", __LINE__, #value); exit(1); } } while (0)
SPI_TypeDef AdsTest_Spi1;
static GPIO_TypeDef chip_select;
static SPI_HandleTypeDef spi, wrong_spi;
static st_logger logger;
static st_cal_input calibration;
static st_f4_capture_port capture;
static st_capture offered;
static HAL_StatusTypeDef submit_result, abort_result;
static unsigned offers, cal_offers, errors, cal_errors, aborts, transfers, nops;
static uint16_t transfer_bytes;
static uint8_t *transfer_rx;
static bool cs_high;
static bool clock_fault_in_cal_offer;
static uint32_t primask, now_ms;
void AdsTest_Nop(void) {++nops;}
uint32_t __get_PRIMASK(void) {return primask;}
void __disable_irq(void) {primask = 1;}
void __set_PRIMASK(uint32_t mask) {primask = mask;}
uint32_t HAL_RCC_GetPCLK2Freq(void) {return 96000000u;}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    C(port == &chip_select && pin == 16u);
    cs_high = state == GPIO_PIN_SET;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *handle, uint8_t *tx,
                                             uint8_t *rx, uint16_t bytes)
{
    C(handle == &spi && !cs_high && nops >= 8u);
    C(tx && rx && bytes >= 1u && bytes <= 5u);
    ++transfers;
    if (submit_result == HAL_OK)
    {
        handle->State = HAL_SPI_STATE_BUSY_TX_RX;
        transfer_rx = rx;
        transfer_bytes = bytes;
    }
    return submit_result;
}
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *handle)
{
    C(handle == &spi && primask == 0u);
    ++aborts;
    if (abort_result == HAL_OK)
        handle->State = HAL_SPI_STATE_READY;
    return abort_result;
}
HAL_StatusTypeDef HAL_SPI_Abort_IT(SPI_HandleTypeDef *handle) {return HAL_SPI_Abort(handle);}
bool st_logger_offer_isr(st_logger *owner, const st_capture *value)
{
    C(owner == &logger);
    if (!atomic_load(&owner->capture_enabled))
        return false;
    ++offers;
    offered = *value;
    return true;
}
void st_logger_source_error_isr(st_logger *owner) {C(owner == &logger); ++errors;}
void st_cal_input_error_isr(st_cal_input *input)
{
    if (input && atomic_load_explicit(&input->enabled, memory_order_acquire))
    {
        ++cal_errors;
        atomic_fetch_add_explicit(&input->errors, 1u, memory_order_relaxed);
    }
}
bool st_cal_input_offer_isr(st_cal_input *input, const st_capture *value)
{
    if (clock_fault_in_cal_offer)
    {
        clock_fault_in_cal_offer = false;
        Capture_ClockFaultIsr(&capture);
        atomic_store_explicit(&logger.capture_enabled, false, memory_order_release);
    }
    if (!input || !value || !atomic_load_explicit(&input->enabled, memory_order_acquire))
        return false;
    C(value != NULL);
    ++cal_offers;
    return true;
}
static uint32_t AdsTest_NowMs(void) {return now_ms;}
static void AdsTest_Reset(bool logging)
{
    memset(&logger, 0, sizeof(logger));
    memset(&calibration, 0, sizeof(calibration));
    memset(&spi, 0, sizeof(spi));
    atomic_init(&logger.capture_enabled, logging);
    atomic_init(&calibration.enabled, true);
    atomic_init(&calibration.errors, 0);
    logger.generation = 7u;
    spi.Instance = SPI1;
    spi.hdmarx = spi.hdmatx = &spi;
    spi.State = HAL_SPI_STATE_READY;
    spi.Init = (SPI_InitTypeDef){.Mode = SPI_MODE_MASTER, .Direction = SPI_DIRECTION_2LINES,
        .DataSize = SPI_DATASIZE_8BIT, .CLKPolarity = SPI_POLARITY_LOW,
        .CLKPhase = SPI_PHASE_2EDGE, .FirstBit = SPI_FIRSTBIT_MSB, .NSS = SPI_NSS_SOFT,
        .CRCCalculation = SPI_CRCCALCULATION_DISABLE, .BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32};
    offers = cal_offers = errors = cal_errors = aborts = transfers = nops = 0;
    primask = now_ms = 0;
    submit_result = abort_result = HAL_OK;
    cs_high = true;
    clock_fault_in_cal_offer = false;
    st_f4_capture_init(&capture, &spi, &chip_select, 16u, &logger, true, 100000u);
    st_f4_capture_bind_calibration(&capture, &calibration);
}
static void AdsTest_Complete(uint8_t msb, uint8_t mid, uint8_t lsb)
{
    C(transfer_bytes == ADS1220_DATA_BYTES);
    transfer_rx[0] = msb;
    transfer_rx[1] = mid;
    transfer_rx[2] = lsb;
    spi.State = HAL_SPI_STATE_READY;
    st_f4_capture_complete(&capture, &spi);
}
static void AdsTest_Capture(void)
{
    AdsTest_Reset(true);
    C(Capture_CheckTimeout(&capture, 100200u) == ADS1220_OK);
    st_f4_capture_drdy(&capture, 100506u);
    C(atomic_load(&capture.busy) && transfers == 1u && transfer_bytes == 3u);
    st_f4_capture_complete(&capture, &wrong_spi);
    C(offers == 0u && atomic_load(&capture.busy));
    AdsTest_Complete(0xff, 0xff, 0xff);
    C(!atomic_load(&capture.busy) && cs_high && offers == 1u && cal_offers == 1u);
    C(offered.sequence == 1u && offered.time_us == 100506u);
    C(Ads1220_DecodeRaw24(offered.bytes) == -1);
    for (unsigned i = 3; i < sizeof(offered.bytes); ++i)
        C(offered.bytes[i] == 0u);
    st_f4_capture_drdy(&capture, 101012u);
    ++logger.generation;
    AdsTest_Complete(0x80, 0, 0);
    C(offers == 1u && cal_offers == 2u); /* No old generation leaks into new file. */
    st_f4_capture_drdy(&capture, 101518u);
    C(Capture_Quiesce(&capture) == ADS1220_BUSY);
    st_f4_capture_drdy(&capture, 102024u);
    C(capture.sequence == 3u);
    AdsTest_Complete(0, 0, 0);
    C(Capture_Quiesce(&capture) == ADS1220_OK && offers == 2u);
    C(Capture_CheckTimeout(&capture, 200000u) == ADS1220_OK);

    AdsTest_Reset(false);
    st_f4_capture_drdy(&capture, 100506u);
    atomic_store(&logger.capture_enabled, true);
    AdsTest_Complete(0, 0, 1);
    C(offers == 0u && cal_offers == 1u); /* IDLE edge cannot become a START sample. */
    st_f4_capture_drdy(&capture, 101012u);
    st_f4_capture_drdy(&capture, 101518u);
    C(atomic_load(&capture.dma_busy) == 1u && errors == 1u);
    AdsTest_Complete(0, 0, 2);
    C(offered.sequence == 2u);
    C(Capture_CheckTimeout(&capture, 101600u) == ADS1220_SPI_ERROR);
    C(atomic_load(&capture.quiescing));

    AdsTest_Reset(true);
    st_f4_capture_drdy(&capture, 100506u);
    AdsTest_Complete(0, 0, 0);
    st_f4_capture_drdy(&capture, 101518u);
    C(capture.sequence == 3u && atomic_load(&capture.discontinuities) == 1u && errors == 1u);
    C(capture.drdy_edges == 2u); /* Inferred missing conversion never counts as a measured edge. */
    AdsTest_Complete(0, 0, 0);
    C(offered.sequence == 3u && offered.time_us == 101518u);
    C(Capture_CheckTimeout(&capture, 101600u) == ADS1220_SPI_ERROR);
    C(Capture_ResumeAfterIdle(&capture, 102000u) == ADS1220_INVALID_STATE);

    AdsTest_Reset(false);
    st_f4_capture_drdy(&capture, 100506u);
    AdsTest_Complete(0, 0, 1);
    C(Capture_Quiesce(&capture) == ADS1220_OK);
    st_f4_capture_drdy(&capture, 101012u);
    C(capture.drdy_edges == 1u);
    C(Capture_CheckTimeout(&capture, 200000u) == ADS1220_OK);
    atomic_store(&calibration.enabled, false);
    C(Capture_ResumeAfterIdle(&capture, 200000u) == ADS1220_OK);
    C(capture.drdy_edges == 1u && offers == 0u);
    st_f4_capture_drdy(&capture, 200506u);
    AdsTest_Complete(0, 0, 2);
    C(capture.drdy_edges == 2u && atomic_load(&capture.discontinuities) == 0u);
    C(Capture_CheckTimeout(&capture, 200600u) == ADS1220_OK);

    AdsTest_Reset(false);
    C(Capture_CheckTimeout(&capture, 110000u) == ADS1220_TIMEOUT);
    C(atomic_load(&capture.quiescing) && atomic_load(&capture.dma_errors) == 1u);
    C(errors == 0u && cal_errors == 1u);
    C(Capture_CheckTimeout(&capture, 111000u) == ADS1220_TIMEOUT);
    C(cal_errors == 1u); /* Faults latch and do not inflate repeated polls. */

    AdsTest_Reset(true);
    st_f4_capture_drdy(&capture, 100506u);
    C(Capture_CheckTimeout(&capture, 101506u) == ADS1220_TIMEOUT);
    C(aborts == 1u && !atomic_load(&capture.busy) && cs_high && errors == 1u);
    st_f4_capture_complete(&capture, &spi);
    C(offers == 0u); /* Late completion after timeout cannot publish data. */
    C(Capture_Quiesce(&capture) == ADS1220_OK);

    AdsTest_Reset(true);
    submit_result = HAL_ERROR;
    st_f4_capture_drdy(&capture, 100506u);
    C(errors == 1u && atomic_load(&capture.dma_errors) == 1u && cs_high);

    AdsTest_Reset(true);
    st_f4_capture_drdy(&capture, 100506u);
    Capture_ClockFaultIsr(&capture);
    C(atomic_load(&capture.discard_pending) && atomic_load(&capture.quiescing));
    C(!atomic_load(&calibration.enabled) && atomic_load(&calibration.errors) == 1u);
    Capture_ClockFaultIsr(&capture);
    C(cal_errors == 1u && aborts == 0u); /* NMI performs no blocking HAL operation. */
    AdsTest_Complete(0, 0, 1);
    C(offers == 0u && cal_offers == 0u && !atomic_load(&capture.busy));
    C(Capture_CheckTimeout(&capture, 100600u) == ADS1220_SPI_ERROR);
    C(Capture_ResumeAfterIdle(&capture, 101000u) == ADS1220_INVALID_STATE);
    st_f4_capture_drdy(&capture, 101012u);
    C(capture.drdy_edges == 1u && transfers == 1u);

    AdsTest_Reset(true);
    st_f4_capture_drdy(&capture, 100506u);
    Capture_ClockFaultIsr(&capture);
    C(Capture_CheckTimeout(&capture, 100600u) == ADS1220_SPI_ERROR);
    C(aborts == 1u && !atomic_load(&capture.busy));
    st_f4_capture_complete(&capture, &spi);
    C(offers == 0u && cal_offers == 0u);

    AdsTest_Reset(true);
    st_f4_capture_drdy(&capture, 100506u);
    clock_fault_in_cal_offer = true; /* NMI after capture's discard check. */
    AdsTest_Complete(0, 0, 1);
    C(offers == 0u && cal_offers == 0u && cal_errors == 1u);
    C(!atomic_load(&calibration.enabled) && atomic_load(&calibration.errors) == 1u);

    AdsTest_Reset(false);
    st_f4_capture_bind_calibration(&capture, NULL);
    Capture_ClockFaultIsr(&capture);
    C(cal_errors == 0u && aborts == 0u);
    Capture_ClockFaultIsr(NULL);
    C(Capture_CheckTimeout(&capture, 100600u) == ADS1220_SPI_ERROR);
    AdsTest_Reset(true);
    st_f4_capture_drdy(&capture, 100506u);
    st_f4_capture_error(&capture, &wrong_spi);
    C(errors == 0u);
    st_f4_capture_error(&capture, &spi);
    C(errors == 1u && atomic_load(&capture.dma_errors) == 1u && cs_high);
}
static void AdsTest_Hal(void)
{
    AdsTest_Reset(false);
    st_ads_hal port;
    C(st_ads_hal_bind(&port, &spi, &chip_select, 16u, AdsTest_NowMs, 20u, true));
    st_ads_port bus = st_ads_hal_port(&port);
    uint8_t tx[5] = {0x23, 0, 0, 0, 0}, rx[5] = {0};
    C(bus.submit(bus.ctx, tx, rx, 5u) == ST_IO_DONE && transfer_bytes == 5u);
    C(bus.submit(bus.ctx, tx, rx, 5u) == ST_IO_WAIT);
    st_ads_hal_complete(&port, &wrong_spi);
    C(bus.poll(bus.ctx) == ST_IO_WAIT);
    spi.State = HAL_SPI_STATE_READY;
    st_ads_hal_complete(&port, &spi);
    C(cs_high && bus.poll(bus.ctx) == ST_IO_DONE && st_ads_hal_idle(&port));
    C(bus.submit(bus.ctx, tx, rx, 0u) == ST_IO_ERROR);
    C(bus.submit(bus.ctx, tx, rx, 6u) == ST_IO_ERROR);
    C(bus.submit(bus.ctx, tx, rx, 1u) == ST_IO_DONE);
    now_ms = 20u;
    C(bus.poll(bus.ctx) == ST_IO_ERROR && aborts == 1u && cs_high);
    C(bus.submit(bus.ctx, tx, rx, 1u) == ST_IO_ERROR);
    C(spi.State == HAL_SPI_STATE_READY);
    C(st_ads_hal_bind(&port, &spi, &chip_select, 16u, AdsTest_NowMs, 20u, true));
    C(bus.submit(bus.ctx, tx, rx, 1u) == ST_IO_DONE);
    spi.State = HAL_SPI_STATE_READY;
    st_ads_hal_complete(&port, &spi);
    C(bus.poll(bus.ctx) == ST_IO_DONE);
    spi.Init.CLKPhase = 0;
    C(!st_ads_hal_bind(&port, &spi, &chip_select, 16u, AdsTest_NowMs, 20u, true));
    spi.Init.CLKPhase = SPI_PHASE_2EDGE;
    spi.Init.BaudRatePrescaler = 0;
    C(!st_ads_hal_bind(&port, &spi, &chip_select, 16u, AdsTest_NowMs, 20u, true));
}
int main(void)
{
    AdsTest_Capture();
    AdsTest_Hal();
    printf("PASS ADS1220 actual capture/HAL model: %u assertions; 3-byte DMA, IRQ identity, timestamps, gaps, idle faults, abort, generation/STOP fencing, SPI/CS timing, CSS discard/calibration invalidation, HAL rebind. No hardware accessed.\n", checks);
    return 0;
}
