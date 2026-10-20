#include "st_diskio.h"
#include "ff.h"
#include "diskio.h"
#include <string.h>
static st_block_device *volume;
void st_diskio_bind(st_block_device *d)
{
    volume = d;
}
DSTATUS disk_status(BYTE drive)
{
    if (drive || !volume)
        return STA_NOINIT;
    if (!st_block_present(volume))
        return STA_NOINIT | STA_NODISK;
    return volume->initialized && !volume->fault ? 0 : STA_NOINIT;
}
DSTATUS disk_initialize(BYTE drive)
{
    if (drive || !volume)
        return STA_NOINIT;
    (void)st_block_initialize(volume);
    return disk_status(drive);
}
static DRESULT result(st_block_result r)
{
    switch (r)
    {
    case ST_BLOCK_OK:
        return RES_OK;
    case ST_BLOCK_PARAMETER:
        return RES_PARERR;
    case ST_BLOCK_NOT_READY:
        return RES_NOTRDY;
    default:
        return RES_ERROR;
    }
}
DRESULT disk_read(BYTE drive, BYTE *data, DWORD first, UINT count)
{
    return drive || !volume ? RES_NOTRDY
                            : result(st_block_transfer(volume, false, first, count, data));
}
DRESULT disk_write(BYTE drive, const BYTE *data, DWORD first, UINT count)
{
    return drive || !volume
               ? RES_NOTRDY
               : result(st_block_transfer(volume, true, first, count, (uint8_t *)(uintptr_t)data));
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *data)
{
    if (drive || !volume || disk_status(drive))
        return RES_NOTRDY;
    if (command == CTRL_SYNC)
        return result(st_block_sync(volume));
    if (!data)
        return RES_PARERR;
    if (command == GET_SECTOR_COUNT)
    {
        DWORD v = volume->sector_count;
        memcpy(data, &v, sizeof(v));
        return RES_OK;
    }
    if (command == GET_SECTOR_SIZE)
    {
        WORD v = ST_BLOCK_BYTES;
        memcpy(data, &v, sizeof(v));
        return RES_OK;
    }
    if (command == GET_BLOCK_SIZE && volume->erase_sectors)
    {
        DWORD v = volume->erase_sectors;
        memcpy(data, &v, sizeof(v));
        return RES_OK;
    }
    return RES_PARERR; /* Unknown erase geometry is not invented. mkfs is disabled. */
}
