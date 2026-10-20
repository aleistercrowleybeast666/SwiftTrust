#ifndef ST_DISKIO_H
#define ST_DISKIO_H
#include "st_block.h"
/* FatFs R0.12c, one volume. Bind only from the exclusive storage-task owner.
 * No implicit formatting, card reinitialization or hot-removal recovery. */
void st_diskio_bind(st_block_device *);
#endif
