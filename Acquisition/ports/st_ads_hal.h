#ifndef ST_ADS_HAL_H
#define ST_ADS_HAL_H
#include "st_ads.h"
#include "stm32f4xx_hal.h"
typedef struct
{
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs, *reset;
    uint16_t cs_pin, reset_pin;
    uint32_t (*now_ms)(void);
    uint32_t started_ms, timeout_ms;
    _Atomic(unsigned) transfer;
    bool verified;
} st_ads_hal;
/* Config-phase owner only. Handles already use SPI1 mode1/8bit/MSB and DMA.
 * Pins, CLKIN, analog profile and reset pin electrical behavior are external.
 * Quiesce this owner before passing the same SPI to st_f4_capture. */
bool st_ads_hal_bind(st_ads_hal *, SPI_HandleTypeDef *, GPIO_TypeDef *cs, uint16_t cs_pin,
                     GPIO_TypeDef *reset, uint16_t reset_pin, uint32_t (*now_ms)(void),
                     uint32_t timeout_ms, bool verified);
st_ads_port st_ads_hal_port(st_ads_hal *);
void st_ads_hal_complete(st_ads_hal *, SPI_HandleTypeDef *);
void st_ads_hal_error(st_ads_hal *, SPI_HandleTypeDef *);
bool st_ads_hal_idle(const st_ads_hal *);
#endif
