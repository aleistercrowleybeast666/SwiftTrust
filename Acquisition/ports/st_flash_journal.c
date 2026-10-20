#if defined(ST_REFERENCE_APPLICATION)
#include "st_flash_journal.h"
#if defined(STM32G071xx)
#include "stm32g0xx_hal.h"
#else
#include "stm32f4xx_hal.h"
#endif
#include <string.h>
#if defined(STM32G071xx)
static const uintptr_t address[2] = {0x0801F000u, 0x0801F800u};
#define REGION_BYTES 2048u
#else
static const uintptr_t address[2] = {0x08008000u, 0x0800C000u};
#define REGION_BYTES 16384u
#endif
static bool read_bytes(void *ctx, unsigned r, uint32_t at, uint8_t *out, size_t n)
{
    (void)ctx;
    if (r > 1 || at > REGION_BYTES || n > REGION_BYTES - at)
        return false;
    memcpy(out, (const void *)(address[r] + at), n);
    return true;
}
static bool idle(void *ctx)
{
    st_flash_window *w = ctx;
    return !__get_IPSR() && w->idle && w->idle(w->ctx);
}
static bool program(void *ctx, unsigned r, uint32_t at, const uint8_t data[8])
{
    if (!idle(ctx) || r > 1 || at % 8 || at > REGION_BYTES - 8)
        return false;
    if (HAL_FLASH_Unlock() != HAL_OK)
        return false;
    HAL_StatusTypeDef result;
#if defined(STM32G071xx)
    uint64_t value;
    memcpy(&value, data, 8);
    result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address[r] + at, value);
#else
    uint32_t low = st_get32(data), high = st_get32(data + 4);
    result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address[r] + at, low);
    if (result == HAL_OK)
        result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address[r] + at + 4, high);
    /* Invalidate ART caches after programming the journal, never execute from it. */
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
#endif
    (void)HAL_FLASH_Lock();
    return result == HAL_OK && !memcmp((const void *)(address[r] + at), data, 8);
}
static bool erase(void *ctx, unsigned r)
{
    if (!idle(ctx) || r > 1 || HAL_FLASH_Unlock() != HAL_OK)
        return false;
    FLASH_EraseInitTypeDef e = {0};
    uint32_t error = 0;
#if defined(STM32G071xx)
    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.Page = 62u + r;
    e.NbPages = 1;
#else
    e.TypeErase = FLASH_TYPEERASE_SECTORS;
    e.Sector = FLASH_SECTOR_2 + r;
    e.NbSectors = 1;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
#endif
    HAL_StatusTypeDef result = HAL_FLASHEx_Erase(&e, &error);
    (void)HAL_FLASH_Lock();
#if !defined(STM32G071xx)
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
#endif
    if (result != HAL_OK)
        return false;
    const uint32_t *p = (const uint32_t *)address[r];
    for (unsigned i = 0; i < REGION_BYTES / 4; i++)
        if (p[i] != UINT32_MAX)
            return false;
    return true;
}
st_journal_port st_flash_journal_port(st_flash_window *w)
{
#if defined(STM32G071xx)
    bool density = *(const uint16_t *)FLASHSIZE_BASE == 128;
#else
    bool density = *(const uint16_t *)FLASHSIZE_BASE == 256;
#endif
    return density ? (st_journal_port){w, read_bytes, program, erase, idle, REGION_BYTES}
                   : (st_journal_port){0};
}
#endif
