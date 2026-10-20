#include "st_ads_hal.h"
#include <string.h>
enum
{
    ADS_IDLE,
    ADS_BUSY,
    ADS_DONE,
    ADS_FAILED
};
bool st_ads_hal_bind(st_ads_hal *p, SPI_HandleTypeDef *spi, GPIO_TypeDef *cs, uint16_t cs_pin,
                     GPIO_TypeDef *reset, uint16_t reset_pin, uint32_t (*now)(void),
                     uint32_t timeout, bool verified)
{
    memset(p, 0, sizeof(*p));
    atomic_init(&p->transfer, ADS_IDLE);
    p->spi = spi;
    p->cs = cs;
    p->cs_pin = cs_pin;
    p->reset = reset;
    p->reset_pin = reset_pin;
    p->now_ms = now;
    p->timeout_ms = timeout;
    p->verified =
        verified && spi && spi->Instance == SPI1 && spi->hdmarx && spi->hdmatx && cs && cs_pin &&
        reset && reset_pin && now && timeout && timeout <= 100 &&
        spi->Init.Mode == SPI_MODE_MASTER && spi->Init.Direction == SPI_DIRECTION_2LINES &&
        spi->Init.DataSize == SPI_DATASIZE_8BIT && spi->Init.CLKPolarity == SPI_POLARITY_LOW &&
        spi->Init.CLKPhase == SPI_PHASE_2EDGE && spi->Init.FirstBit == SPI_FIRSTBIT_MSB &&
        spi->Init.NSS == SPI_NSS_SOFT && spi->Init.CRCCalculation == SPI_CRCCALCULATION_DISABLE;
    return p->verified;
}
static void reset_pin(void *ctx, bool asserted)
{
    st_ads_hal *p = ctx;
    if (p->verified)
        HAL_GPIO_WritePin(p->reset, p->reset_pin, asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
static st_async_result submit(void *ctx, const uint8_t tx[12], uint8_t rx[12])
{
    st_ads_hal *p = ctx;
    if (!p->verified)
        return ST_IO_ERROR;
    unsigned state = atomic_load_explicit(&p->transfer, memory_order_acquire);
    if (state == ADS_FAILED)
        return ST_IO_ERROR;
    if (state != ADS_IDLE)
        return ST_IO_WAIT;
    p->started_ms = p->now_ms();
    atomic_store_explicit(&p->transfer, ADS_BUSY, memory_order_release);
    HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef result =
        HAL_SPI_TransmitReceive_DMA(p->spi, (uint8_t *)(uintptr_t)tx, rx, 12);
    if (result == HAL_OK)
        return ST_IO_DONE;
    HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_SET);
    atomic_store_explicit(&p->transfer, result == HAL_BUSY ? ADS_IDLE : ADS_FAILED,
                          memory_order_release);
    return result == HAL_BUSY ? ST_IO_WAIT : ST_IO_ERROR;
}
static st_async_result poll(void *ctx)
{
    st_ads_hal *p = ctx;
    unsigned state = atomic_load_explicit(&p->transfer, memory_order_acquire);
    if (!p->verified || state == ADS_FAILED)
        return ST_IO_ERROR;
    if (state == ADS_DONE)
    {
        atomic_store_explicit(&p->transfer, ADS_IDLE, memory_order_release);
        return ST_IO_DONE;
    }
    if (state == ADS_BUSY && (uint32_t)(p->now_ms() - p->started_ms) >= p->timeout_ms)
    {
        atomic_store_explicit(&p->transfer, ADS_FAILED, memory_order_release);
        (void)HAL_SPI_Abort_IT(p->spi);
        HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_SET);
        return ST_IO_ERROR;
    }
    return ST_IO_WAIT;
}
st_ads_port st_ads_hal_port(st_ads_hal *p)
{
    return (st_ads_port){p, reset_pin, submit, poll};
}
void st_ads_hal_complete(st_ads_hal *p, SPI_HandleTypeDef *spi)
{
    if (p->verified && spi == p->spi &&
        atomic_load_explicit(&p->transfer, memory_order_acquire) == ADS_BUSY)
    {
        HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_SET);
        atomic_store_explicit(&p->transfer, ADS_DONE, memory_order_release);
    }
}
void st_ads_hal_error(st_ads_hal *p, SPI_HandleTypeDef *spi)
{
    if (p->verified && spi == p->spi &&
        atomic_load_explicit(&p->transfer, memory_order_acquire) == ADS_BUSY)
    {
        HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_SET);
        atomic_store_explicit(&p->transfer, ADS_FAILED, memory_order_release);
    }
}
bool st_ads_hal_idle(const st_ads_hal *p)
{
    return p->verified && atomic_load_explicit(&p->transfer, memory_order_acquire) == ADS_IDLE &&
           p->spi->State == HAL_SPI_STATE_READY;
}
