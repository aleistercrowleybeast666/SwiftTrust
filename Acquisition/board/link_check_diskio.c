/* Link-check-only storage backend: explicitly NOT READY, never reports success.
 * Replace with reviewed SDIO diskio+storage-task binding before operational use. */
#include "ff.h"
#include "diskio.h"
DSTATUS disk_initialize(BYTE d)
{
    (void)d;
    return STA_NOINIT;
}
DSTATUS disk_status(BYTE d)
{
    (void)d;
    return STA_NOINIT;
}
DRESULT disk_read(BYTE d, BYTE *p, DWORD s, UINT n)
{
    (void)d;
    (void)p;
    (void)s;
    (void)n;
    return RES_NOTRDY;
}
DRESULT disk_write(BYTE d, const BYTE *p, DWORD s, UINT n)
{
    (void)d;
    (void)p;
    (void)s;
    (void)n;
    return RES_NOTRDY;
}
DRESULT disk_ioctl(BYTE d, BYTE c, void *p)
{
    (void)d;
    (void)c;
    (void)p;
    return RES_NOTRDY;
}
