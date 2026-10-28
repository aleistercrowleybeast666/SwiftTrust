#include "st_f4_capture.h"
#include <string.h>
#define CAPTURE_DMA_TIMEOUT_US 1000u
static void increment(_Atomic(uint32_t) *v)
{
    uint32_t n = atomic_load_explicit(v, memory_order_relaxed);
    if (n != UINT32_MAX)
        atomic_store_explicit(v, n + 1, memory_order_relaxed);
}
void st_f4_capture_init(st_f4_capture_port *p, SPI_HandleTypeDef *spi, GPIO_TypeDef *cs,
                        uint16_t pin, st_logger *l, bool verified, uint64_t started_us)
{
    memset(p, 0, sizeof(*p));
    p->spi = spi;
    p->cs_port = cs;
    p->cs_pin = pin;
    p->logger = l;
    p->verified = verified && spi && cs && pin && l;
    p->last_edge_time_us = started_us;
    atomic_init(&p->busy, false);
    atomic_init(&p->quiescing, false);
    atomic_init(&p->discard_pending, false);
    atomic_init(&p->dma_busy, 0);
    atomic_init(&p->dma_errors, 0);
    atomic_init(&p->discontinuities, 0);
}
void st_f4_capture_bind_calibration(st_f4_capture_port *p, st_cal_input *q)
{
    p->calibration = q;
}
void st_f4_capture_drdy(st_f4_capture_port *p, uint64_t timestamp)
{
    if (atomic_load_explicit(&p->quiescing, memory_order_acquire))
        return;
    ++p->drdy_edges;
    ++p->sequence; /* Advances even on DMA busy; missing samples remain visible. */
    if (!p->verified)
        return;
    bool logging = atomic_load_explicit(&p->logger->capture_enabled, memory_order_acquire);
    if (p->have_edge)
    {
        uint64_t elapsed = timestamp >= p->last_edge_time_us ? timestamp - p->last_edge_time_us : 0;
        uint64_t period = (uint64_t)ST_ADC_CONVERSION_CYCLES * 1000000u;
        uint64_t clocks = elapsed <= UINT32_MAX ? elapsed * ST_ADC_CLOCK_HZ : UINT64_MAX;
        if (clocks < period / 2u || clocks > period + period / 2u)
        {
            increment(&p->discontinuities);
            st_cal_input_error_isr(p->calibration);
            if (logging)
                st_logger_source_error_isr(p->logger);
            /* Sequence counts estimated missed conversions; no artificial
             * records are inserted and the discontinuity remains an error. */
            if (elapsed <= UINT32_MAX && clocks > period + period / 2u)
                p->sequence += (uint32_t)((clocks + period / 2u) / period - 1u);
        }
    }
    p->last_edge_time_us = timestamp;
    p->have_edge = true;
    if (atomic_load_explicit(&p->busy, memory_order_acquire))
    {
        increment(&p->dma_busy);
        st_cal_input_error_isr(p->calibration);
        if (logging)
            st_logger_source_error_isr(p->logger);
        return;
    }
    p->pending_logging = logging;
    p->pending_sequence = p->sequence;
    p->pending_time_us = timestamp;
    p->pending_generation = p->logger->generation;
    atomic_store_explicit(&p->busy, true, memory_order_release);
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_RESET);
    /* Eight cycles at <=100 MHz satisfy ADS1220 CS setup >=50 ns. */
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
    HAL_StatusTypeDef result =
        HAL_SPI_TransmitReceive_DMA(p->spi, p->tx, p->rx, ADS1220_DATA_BYTES);
    if (result != HAL_OK)
    {
        st_cal_input_error_isr(p->calibration);
        HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
        atomic_store_explicit(&p->busy, false, memory_order_release);
        if (result == HAL_BUSY)
            increment(&p->dma_busy);
        else
            increment(&p->dma_errors);
        if (logging)
            st_logger_source_error_isr(p->logger);
    }
}
void st_f4_capture_complete(st_f4_capture_port *p, SPI_HandleTypeDef *spi)
{
    if (!p->verified || spi != p->spi || !atomic_load_explicit(&p->busy, memory_order_acquire))
        return;
    /* HAL completion follows SPI BSY clear; hold CS for another >=25 ns. */
    __NOP(); __NOP(); __NOP(); __NOP();
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    st_capture c = {.time_us = p->pending_time_us, .sequence = p->pending_sequence};
    if (atomic_load_explicit(&p->discard_pending, memory_order_acquire))
    {
        atomic_store_explicit(&p->busy, false, memory_order_release);
        return;
    }
    memcpy(c.bytes, p->rx, ADS1220_DATA_BYTES);
    (void)st_cal_input_offer_isr(p->calibration, &c);
    if (p->pending_logging && p->pending_generation == p->logger->generation)
        (void)st_logger_offer_isr(p->logger, &c);
    atomic_store_explicit(&p->busy, false, memory_order_release);
}
Ads1220Result Capture_Quiesce(st_f4_capture_port *p)
{
    if (!p || !p->verified)
        return ADS1220_INVALID_STATE;
    atomic_store_explicit(&p->quiescing, true, memory_order_release);
    return atomic_load_explicit(&p->busy, memory_order_acquire) ? ADS1220_BUSY : ADS1220_OK;
}
void Capture_ClockFaultIsr(st_f4_capture_port *p)
{
    if (!p)
        return;
    atomic_store_explicit(&p->discard_pending, true, memory_order_release);
    atomic_store_explicit(&p->quiescing, true, memory_order_release);
    /* Invalidate a collection even if NMI interrupted the completion after
     * its discard check or interrupted the tap after its enabled check. */
    st_cal_input_error_isr(p->calibration);
    if (p->calibration)
        atomic_store_explicit(&p->calibration->enabled, false, memory_order_release);
}
Ads1220Result Capture_CheckTimeout(st_f4_capture_port *p, uint64_t now)
{
    if (!p || !p->verified)
        return ADS1220_INVALID_STATE;
    if (p->fault_result != ADS1220_OK)
        return p->fault_result;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool busy = atomic_load_explicit(&p->busy, memory_order_acquire);
    bool transfer_timeout = busy && now >= p->pending_time_us &&
                            now - p->pending_time_us >= CAPTURE_DMA_TIMEOUT_US;
    bool stalled = !atomic_load_explicit(&p->quiescing, memory_order_acquire) &&
                   now >= p->last_edge_time_us && now - p->last_edge_time_us >= ST_ADC_FRESH_US;
    bool failed = atomic_load_explicit(&p->dma_errors, memory_order_acquire) ||
                  atomic_load_explicit(&p->dma_busy, memory_order_acquire) ||
                  atomic_load_explicit(&p->discontinuities, memory_order_acquire) ||
                  atomic_load_explicit(&p->discard_pending, memory_order_acquire);
    if (!transfer_timeout && !stalled && !failed)
    {
        __set_PRIMASK(mask);
        return ADS1220_OK;
    }
    atomic_store_explicit(&p->quiescing, true, memory_order_release);
    atomic_store_explicit(&p->busy, false, memory_order_release);
    p->pending_logging = false;
    p->fault_result = transfer_timeout || stalled ? ADS1220_TIMEOUT : ADS1220_SPI_ERROR;
    __set_PRIMASK(mask);
    /* IRQ callbacks now reject this transfer. Abort in foreground with IRQs
     * restored: HAL timeout accounting and DMA shutdown must remain live. */
    if (busy && HAL_SPI_Abort(p->spi) != HAL_OK)
        p->fault_result = ADS1220_SPI_ERROR;
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    if (transfer_timeout || stalled)
    {
        increment(&p->dma_errors);
        st_cal_input_error_isr(p->calibration);
        if (atomic_load_explicit(&p->logger->capture_enabled, memory_order_acquire))
            st_logger_source_error_isr(p->logger);
    }
    return p->fault_result;
}
Ads1220Result Capture_ResumeAfterIdle(st_f4_capture_port *p, uint64_t now)
{
    if (!p || !p->verified || p->fault_result != ADS1220_OK ||
        atomic_load_explicit(&p->discard_pending, memory_order_acquire) ||
        atomic_load_explicit(&p->logger->capture_enabled, memory_order_acquire) ||
        (p->calibration && atomic_load_explicit(&p->calibration->enabled, memory_order_acquire)))
        return ADS1220_INVALID_STATE;
    if (atomic_load_explicit(&p->busy, memory_order_acquire))
        return ADS1220_BUSY;
    if (atomic_load_explicit(&p->dma_errors, memory_order_acquire) ||
        atomic_load_explicit(&p->dma_busy, memory_order_acquire) ||
        atomic_load_explicit(&p->discontinuities, memory_order_acquire))
        return ADS1220_SPI_ERROR;
    p->have_edge = false;
    p->last_edge_time_us = now;
    atomic_store_explicit(&p->quiescing, false, memory_order_release);
    return ADS1220_OK;
}
void st_f4_capture_error(st_f4_capture_port *p, SPI_HandleTypeDef *spi)
{
    if (!p->verified || spi != p->spi || !atomic_load_explicit(&p->busy, memory_order_acquire))
        return;
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    increment(&p->dma_errors);
    st_cal_input_error_isr(p->calibration);
    if (p->pending_logging && p->pending_generation == p->logger->generation)
        st_logger_source_error_isr(p->logger);
    atomic_store_explicit(&p->busy, false, memory_order_release);
}
bool st_f4_voltage_leds(const st_f4_led_port *p, st_voltage_view v)
{
    if (!p->verified)
        return false;
    for (unsigned i = 0; i < 4; i++)
        if (!p->port[i] || !p->pin[i])
            return false;
    uint8_t mask =
        (v.kind == ST_V_MEASURED || v.kind == ST_V_LOW) ? (uint8_t)(v.led_mask & 15u) : 0;
    for (unsigned i = 0; i < 4; i++)
    {
        bool on = (mask & (1u << i)) != 0;
        HAL_GPIO_WritePin(p->port[i], p->pin[i],
                          on == p->active_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    return true;
}
