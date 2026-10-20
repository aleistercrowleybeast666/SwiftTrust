#ifndef ST_JOURNAL_H
#define ST_JOURNAL_H
#include "st_nv.h"
/* Two physical erase regions; all programming is aligned 8-byte increments.
 * Program must be synchronous + readback. Erase/write permitted only at idle.
 * A torn frame or erase fails closed: never roll counters back to an old image. */
typedef struct
{
    void *ctx;
    bool (*read)(void *, unsigned region, uint32_t offset, uint8_t *, size_t);
    bool (*program8)(void *, unsigned region, uint32_t offset, const uint8_t[8]);
    bool (*erase)(void *, unsigned region);
    bool (*idle)(void *);
    uint32_t region_bytes;
} st_journal_port;
typedef struct
{
    st_journal_port port;
    uint8_t image[2][ST_NV_BYTES];
    uint64_t sequence;
    uint32_t next;
    unsigned region;
    bool fault, opened;
} st_journal;
bool st_journal_open(st_journal *, st_journal_port);
st_nv_storage st_journal_storage(st_journal *);
#endif
