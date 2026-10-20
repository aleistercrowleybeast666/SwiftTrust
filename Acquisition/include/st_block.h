#ifndef ST_BLOCK_H
#define ST_BLOCK_H
#include "st_core.h"
enum
{
    ST_BLOCK_BYTES = 512,
    ST_BLOCK_MAX_SECTORS = 128,
    ST_BLOCK_SPIN_LIMIT = 4096
};
typedef struct
{
    void *ctx;
    bool (*present)(void *);
    bool (*ready)(void *);
    st_async_result (*submit)(void *, bool write, uint8_t aligned_sector[512], uint32_t sector);
    st_async_result (*poll)(void *);
    void (*abort)(void *);
    uint32_t (*now_ms)(void *);
    /* Bounded cooperative task yield. Never call this block device from an ISR. */
    void (*yield)(void *);
} st_block_port;
typedef enum
{
    ST_BLOCK_OK,
    ST_BLOCK_NOT_READY,
    ST_BLOCK_PARAMETER,
    ST_BLOCK_ERROR
} st_block_result;
typedef struct
{
    st_block_port port;
    uint32_t sector_count, erase_sectors, timeout_ms;
    uint32_t bounce[ST_BLOCK_BYTES / sizeof(uint32_t)];
    bool bound, initialized, fault, inflight;
} st_block_device;
/* One storage-task owner. Stores callbacks only; no pin/clock/card initialization.
 * The 512-byte bounce sector is DMA-aligned and retains data until completion.
 * Card removal/timeout/I/O error latches fault; only explicit rebind can recover. */
void st_block_bind(st_block_device *, st_block_port, uint32_t sectors, uint32_t erase_sectors,
                   uint32_t timeout_ms, bool hardware_verified);
bool st_block_present(st_block_device *);
bool st_block_initialize(st_block_device *);
st_block_result st_block_transfer(st_block_device *, bool write, uint32_t first, uint32_t count,
                                  uint8_t *data);
st_block_result st_block_sync(st_block_device *);
#endif
