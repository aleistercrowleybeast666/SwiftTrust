#ifndef ST_FATFS_WORKER_H
#define ST_FATFS_WORKER_H
#include "st_core.h"
#include "ff.h"
typedef struct
{
    FIL file;
    _Atomic(unsigned) state;
    st_file_op op;
    const uint8_t *data;
    size_t length, completed;
    uint32_t name_index;
    bool verified, opened;
    FRESULT last_result;
} st_fatfs_worker;
/* A mounted, exclusively owned volume is required. Run the blocking worker
 * in a lower-priority task, or a single owner whose block-device wait yields
 * service the logger without reentering this worker/FatFs (ReferenceApp). */
void st_fatfs_init(st_fatfs_worker *, bool mounted_and_verified);
st_file_port st_fatfs_port(st_fatfs_worker *);
void st_fatfs_worker_step(st_fatfs_worker *);
/* Called by the same volume owner, not a cross-task lock. */
bool st_fatfs_idle(const st_fatfs_worker *);
#endif
