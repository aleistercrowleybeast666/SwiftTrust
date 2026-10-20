#ifndef ST_RADIO_HAL_H
#define ST_RADIO_HAL_H
#include "st_radio.h"
#if defined(STM32G071xx)
#include "stm32g0xx_hal.h"
#else
#include "stm32f4xx_hal.h"
#endif
typedef struct
{
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs, *busy, *reset;
    uint16_t cs_pin, busy_pin, reset_pin;
    void *ctx;
    uint32_t (*now_ms)(void *);
    void (*yield)(void *);
    uint32_t spi_timeout_ms, busy_timeout_ms, reset_low_ms, reset_settle_ms;
    bool verified;
} st_radio_hal;
/* Existing initialized SPI/pin handles, mode0/8bit/MSB, one task owner.
 * Set callbacks/timeouts before bind; verified includes schematic, SPI timing,
 * reset pulse/power settling and live HAL tick. No clock or GPIO setup here.
 * yield is bounded/cooperative; IRQs stay enabled so ADC can preempt radio I/O. */
bool st_radio_hal_bind(st_radio_hal *, bool hardware_verified);
st_radio_port st_radio_hal_port(st_radio_hal *);
#endif
