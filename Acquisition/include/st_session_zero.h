#ifndef __ST_SESSION_ZERO_H
#define __ST_SESSION_ZERO_H
#include "st_core.h"
typedef enum
{
    ST_ZERO_IDLE,
    ST_ZERO_COLLECTING,
    ST_ZERO_READY,
    ST_ZERO_FAILED
} StSessionZeroState;
typedef struct
{
    StSessionZeroState state;
    uint32_t count, sequence, max_span;
    uint64_t began_us, last_us;
    int64_t sum;
    int32_t minimum, maximum, mean;
} StSessionZero;
typedef enum
{
    ST_ZERO_OK,
    ST_ZERO_BUSY,
    ST_ZERO_INVALID
} SessionZeroResult;
SessionZeroResult SessionZero_Begin(StSessionZero *, bool idle, uint64_t now_us, uint32_t max_span);
void SessionZero_Feed(StSessionZero *, const st_capture *);
void SessionZero_Tick(StSessionZero *, uint64_t now_us);
SessionZeroResult SessionZero_Apply(const StSessionZero *, st_logger *);
#endif
