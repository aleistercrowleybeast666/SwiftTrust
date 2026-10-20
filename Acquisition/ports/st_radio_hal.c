#include "st_radio_hal.h"
#include <string.h>
enum
{
    RADIO_POLL_LIMIT = 4096,
    RADIO_COMMAND_MAX = 8,
    RADIO_DATA_MAX = 128
};
static bool single_pin(uint16_t pin)
{
    return pin && !(pin & (pin - 1u));
}
static bool task_context(void)
{
    if (__get_IPSR() || __get_PRIMASK())
        return false;
#if !defined(__ARM_ARCH_6M__)
    if (__get_BASEPRI())
        return false;
#endif
    return true;
}
bool st_radio_hal_bind(st_radio_hal *p, bool verified)
{
    if (!p)
        return false;
    SPI_HandleTypeDef *s = p->spi;
    p->verified = verified && s && p->cs && p->busy && p->reset && single_pin(p->cs_pin) &&
                  single_pin(p->busy_pin) && single_pin(p->reset_pin) && p->now_ms && p->yield &&
                  p->spi_timeout_ms && p->spi_timeout_ms <= 20 && p->busy_timeout_ms &&
                  p->busy_timeout_ms <= 100 && p->reset_low_ms >= 1 && p->reset_low_ms <= 100 &&
                  p->reset_settle_ms >= 5 && p->reset_settle_ms <= 100;
    if (!p->verified)
        return false;
#if defined(STM32G071xx)
    p->verified = s->Instance == SPI1;
#else
    p->verified = s->Instance == SPI2;
#endif
    p->verified = p->verified && s->Init.Mode == SPI_MODE_MASTER &&
                  s->Init.Direction == SPI_DIRECTION_2LINES &&
                  s->Init.DataSize == SPI_DATASIZE_8BIT &&
                  s->Init.CLKPolarity == SPI_POLARITY_LOW && s->Init.CLKPhase == SPI_PHASE_1EDGE &&
                  s->Init.FirstBit == SPI_FIRSTBIT_MSB && s->Init.NSS == SPI_NSS_SOFT &&
                  s->Init.CRCCalculation == SPI_CRCCALCULATION_DISABLE;
    if ((p->cs == p->busy && p->cs_pin == p->busy_pin) ||
        (p->cs == p->reset && p->cs_pin == p->reset_pin) ||
        (p->busy == p->reset && p->busy_pin == p->reset_pin))
        p->verified = false;
    return p->verified;
}
static bool ready(void *ctx)
{
    st_radio_hal *p = ctx;
    return p->verified && HAL_GPIO_ReadPin(p->busy, p->busy_pin) == GPIO_PIN_RESET;
}
static bool wait_not_busy(st_radio_hal *p)
{
    uint32_t began = p->now_ms(p->ctx);
    for (unsigned spins = 0; spins < RADIO_POLL_LIMIT; spins++)
    {
        if (ready(p))
            return true;
        if ((uint32_t)(p->now_ms(p->ctx) - began) >= p->busy_timeout_ms)
            return false;
        p->yield(p->ctx);
    }
    return false; /* A frozen clock cannot cause an unbounded BUSY loop. */
}
static bool wait_ms(st_radio_hal *p, uint32_t duration)
{
    uint32_t began = p->now_ms(p->ctx);
    for (unsigned spins = 0; spins < RADIO_POLL_LIMIT; spins++)
    {
        if ((uint32_t)(p->now_ms(p->ctx) - began) >= duration)
            return true;
        p->yield(p->ctx);
    }
    return false;
}
static bool reset_chip(void *ctx)
{
    st_radio_hal *p = ctx;
    if (!p->verified || !task_context())
        return false;
    HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(p->reset, p->reset_pin, GPIO_PIN_RESET);
    bool low_done = wait_ms(p, p->reset_low_ms);
    HAL_GPIO_WritePin(p->reset, p->reset_pin, GPIO_PIN_SET);
    return low_done && wait_ms(p, p->reset_settle_ms);
}
static bool command(void *ctx, const uint8_t *cmd, uint16_t cn, const uint8_t *tx, uint8_t *rx,
                    uint16_t count)
{
    st_radio_hal *p = ctx;
    if (!p->verified || !task_context() || !cmd || !cn || cn > RADIO_COMMAND_MAX ||
        count > RADIO_DATA_MAX || (count && (!tx && !rx)) || (tx && rx) || !wait_not_busy(p))
        return false;
    HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef result =
        HAL_SPI_Transmit(p->spi, (uint8_t *)(uintptr_t)cmd, cn, p->spi_timeout_ms);
    if (result == HAL_OK && count)
    {
        if (rx)
        {
            memset(rx, 0, count);
            result = HAL_SPI_Receive(p->spi, rx, count, p->spi_timeout_ms);
        }
        else
            result = HAL_SPI_Transmit(p->spi, (uint8_t *)(uintptr_t)tx, count, p->spi_timeout_ms);
    }
    HAL_GPIO_WritePin(p->cs, p->cs_pin, GPIO_PIN_SET); /* Also release on any SPI error. */
    return result == HAL_OK;
}
st_radio_port st_radio_hal_port(st_radio_hal *p)
{
    return (st_radio_port){p, command, reset_chip, ready};
}
