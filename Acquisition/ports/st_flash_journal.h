#ifndef ST_FLASH_JOURNAL_H
#define ST_FLASH_JOURNAL_H
#include "st_journal.h"
/* Reference geometry: F411 sectors 2/3 (16KiB each), G071 pages 62/63
 * (2KiB each). Reference linker scripts exclude them. Preserve on upgrades.
 * Read the flash density register before binding; never erase a different MCU. */
typedef struct
{
    void *ctx;
    bool (*idle)(void *);
} st_flash_window;
st_journal_port st_flash_journal_port(st_flash_window *);
#endif
