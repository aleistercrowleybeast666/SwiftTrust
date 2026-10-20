#include "st_sdio_hal.h"
#include <string.h>
enum
{
    SD_IDLE,
    SD_READING,
    SD_WRITING,
    SD_DONE,
    SD_FAILED
};
static bool task_context(void)
{
    return !__get_IPSR() && !__get_PRIMASK() && !__get_BASEPRI();
}
bool st_sdio_hal_bind(st_sdio_hal *p, SD_HandleTypeDef *sd, void *ctx, bool (*present)(void *),
                      uint32_t (*now)(void *), void (*yield)(void *), bool verified)
{
    memset(p, 0, sizeof(*p));
    atomic_init(&p->transfer, SD_IDLE);
    p->sd = sd;
    p->ctx = ctx;
    p->card_present = present;
    p->now_ms = now;
    p->yield = yield;
    p->verified = verified && sd && sd->Instance == SDIO && sd->hdmarx && sd->hdmatx &&
                  sd->State == HAL_SD_STATE_READY && sd->SdCard.LogBlockSize == ST_BLOCK_BYTES &&
                  sd->SdCard.LogBlockNbr && present && now && yield;
    return p->verified;
}
static bool present(void *ctx)
{
    st_sdio_hal *p = ctx;
    return p->verified && p->card_present(p->ctx);
}
static bool ready(void *ctx)
{
    st_sdio_hal *p = ctx;
    return present(p) && task_context() && p->sd->State == HAL_SD_STATE_READY &&
           HAL_SD_GetCardState(p->sd) == HAL_SD_CARD_TRANSFER;
}
static st_async_result submit(void *ctx, bool write, uint8_t *data, uint32_t sector)
{
    st_sdio_hal *p = ctx;
    if (!present(p) || !task_context() || ((uintptr_t)data & 3u))
        return ST_IO_ERROR;
    unsigned state = atomic_load_explicit(&p->transfer, memory_order_acquire);
    if (state == SD_FAILED)
        return ST_IO_ERROR;
    if (state != SD_IDLE)
        return ST_IO_WAIT;
    atomic_store_explicit(&p->transfer, write ? SD_WRITING : SD_READING, memory_order_release);
    HAL_StatusTypeDef result = write ? HAL_SD_WriteBlocks_DMA(p->sd, data, sector, 1)
                                     : HAL_SD_ReadBlocks_DMA(p->sd, data, sector, 1);
    if (result == HAL_OK)
        return ST_IO_DONE;
    atomic_store_explicit(&p->transfer, result == HAL_BUSY ? SD_IDLE : SD_FAILED,
                          memory_order_release);
    return result == HAL_BUSY ? ST_IO_WAIT : ST_IO_ERROR;
}
static st_async_result poll(void *ctx)
{
    st_sdio_hal *p = ctx;
    unsigned state = atomic_load_explicit(&p->transfer, memory_order_acquire);
    if (!p->verified || state == SD_FAILED)
        return ST_IO_ERROR;
    if (state != SD_DONE)
        return ST_IO_WAIT;
    atomic_store_explicit(&p->transfer, SD_IDLE, memory_order_release);
    return ST_IO_DONE;
}
static void abort_io(void *ctx)
{
    st_sdio_hal *p = ctx;
    atomic_store_explicit(&p->transfer, SD_FAILED, memory_order_release);
    if (p->verified)
        (void)HAL_SD_Abort_IT(p->sd);
    /* Fault stays latched even if Abort callback arrives; caller must reinitialize. */
}
static uint32_t now_ms(void *ctx)
{
    st_sdio_hal *p = ctx;
    return p->now_ms(p->ctx);
}
static void yield_task(void *ctx)
{
    st_sdio_hal *p = ctx;
    p->yield(p->ctx);
}
st_block_port st_sdio_hal_port(st_sdio_hal *p)
{
    return (st_block_port){p, present, ready, submit, poll, abort_io, now_ms, yield_task};
}
static void complete(st_sdio_hal *p, SD_HandleTypeDef *sd, unsigned expected)
{
    if (p->verified && sd == p->sd &&
        atomic_load_explicit(&p->transfer, memory_order_acquire) == expected)
        atomic_store_explicit(&p->transfer, SD_DONE, memory_order_release);
}
void st_sdio_hal_rx_complete(st_sdio_hal *p, SD_HandleTypeDef *sd)
{
    complete(p, sd, SD_READING);
}
void st_sdio_hal_tx_complete(st_sdio_hal *p, SD_HandleTypeDef *sd)
{
    complete(p, sd, SD_WRITING);
}
void st_sdio_hal_error(st_sdio_hal *p, SD_HandleTypeDef *sd)
{
    if (p->verified && sd == p->sd)
        atomic_store_explicit(&p->transfer, SD_FAILED, memory_order_release);
}
