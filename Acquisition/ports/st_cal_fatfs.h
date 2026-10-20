#ifndef ST_CAL_FATFS_H
#define ST_CAL_FATFS_H
#include "st_cal.h"
#include "st_fatfs_worker.h"
typedef enum
{
    ST_CAL_DISK_EMPTY,
    ST_CAL_DISK_VALID,
    ST_CAL_DISK_DAMAGED,
    ST_CAL_DISK_FOREIGN,
    ST_CAL_DISK_IO,
    ST_CAL_DISK_BUSY
} st_cal_disk_state;
typedef struct
{
    st_fatfs_worker *worker;
    st_logger *logger;
    st_cal_binding binding;
    uint64_t generation;
    uint32_t revision;
    unsigned active_slot;
    bool opened, fault;
    st_cal_disk_state state;
    FRESULT last_result;
    uint8_t frame[1024];
} st_cal_fatfs;
/* Exclusive mounted-volume owner only, serialized with logger/worker/commands.
 * Two fixed 1KiB files; no rename/unlink/mkfs, no writes to the accepted slot.
 * Runtime errors latch fault. Explicit remount/reopen reconciles uncertain media.
 * FAT metadata/card FTL cannot provide a general physical power-loss guarantee. */
void st_cal_fatfs_init(st_cal_fatfs *, st_fatfs_worker *, st_logger *, st_cal_binding);
st_cal_disk_state st_cal_fatfs_open(st_cal_fatfs *, st_cal_profile *);
st_cal_storage st_cal_fatfs_storage(st_cal_fatfs *);
#endif
