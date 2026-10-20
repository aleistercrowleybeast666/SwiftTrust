#include "st_f4_capture.h"
#include <string.h>
static void increment(_Atomic(uint32_t) *v)
{
    uint32_t n = atomic_load_explicit(v, memory_order_relaxed);
    if (n != UINT32_MAX)
        atomic_store_explicit(v, n + 1, memory_order_relaxed);
}
void st_f4_capture_init(st_f4_capture_port *p, SPI_HandleTypeDef *spi, GPIO_TypeDef *cs,
                        uint16_t pin, st_logger *l, bool verified)
{
    memset(p, 0, sizeof(*p));
    p->spi = spi;
    p->cs_port = cs;
    p->cs_pin = pin;
    p->logger = l;
    p->verified = verified && spi && cs && pin && l;
    atomic_init(&p->busy, false);
    atomic_init(&p->dma_busy, 0);
    atomic_init(&p->dma_errors, 0);
}
void st_f4_capture_drdy(st_f4_capture_port *p, uint64_t timestamp)
{
    ++p->sequence; /* Advances even on DMA busy; missing samples remain visible. */
    if (!p->verified)
        return;
    bool logging = atomic_load_explicit(&p->logger->capture_enabled, memory_order_acquire);
    if (atomic_load_explicit(&p->busy, memory_order_acquire))
    {
        increment(&p->dma_busy);
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
    HAL_StatusTypeDef result = HAL_SPI_TransmitReceive_DMA(p->spi, p->tx, p->rx, 12);
    if (result != HAL_OK)
    {
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
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    st_capture c = {.time_us = p->pending_time_us, .sequence = p->pending_sequence};
    memcpy(c.bytes, p->rx, 12);
    if (p->pending_logging && p->pending_generation == p->logger->generation)
        (void)st_logger_offer_isr(p->logger, &c);
    atomic_store_explicit(&p->busy, false, memory_order_release);
}
void st_f4_capture_error(st_f4_capture_port *p, SPI_HandleTypeDef *spi)
{
    if (!p->verified || spi != p->spi || !atomic_load_explicit(&p->busy, memory_order_acquire))
        return;
    HAL_GPIO_WritePin(p->cs_port, p->cs_pin, GPIO_PIN_SET);
    increment(&p->dma_errors);
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
