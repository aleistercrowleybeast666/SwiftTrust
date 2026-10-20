#ifndef ST_SDIO_HAL_H
#define ST_SDIO_HAL_H
#include "st_block.h"
#include "stm32f4xx_hal.h"
typedef struct
{
    SD_HandleTypeDef *sd;
    void *ctx;
    bool (*card_present)(void *);
    uint32_t (*now_ms)(void *);
    void (*yield)(void *);
    _Atomic(unsigned) transfer;
    bool verified;
} st_sdio_hal;
/* Caller must initialize SDIO pins/clock/DMA and card, then verify its geometry.
 * PB5 card-detect polarity is supplied by card_present; never guessed here.
 * HAL tick must run; callbacks route to this owner. No hardware is initialized. */
bool st_sdio_hal_bind(st_sdio_hal *, SD_HandleTypeDef *, void *, bool (*present)(void *),
                      uint32_t (*now_ms)(void *), void (*yield)(void *), bool verified);
st_block_port st_sdio_hal_port(st_sdio_hal *);
void st_sdio_hal_rx_complete(st_sdio_hal *, SD_HandleTypeDef *);
void st_sdio_hal_tx_complete(st_sdio_hal *, SD_HandleTypeDef *);
void st_sdio_hal_error(st_sdio_hal *, SD_HandleTypeDef *);
#endif
