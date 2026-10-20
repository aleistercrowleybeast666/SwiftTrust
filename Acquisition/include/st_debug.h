#ifndef ST_DEBUG_H
#define ST_DEBUG_H
#include "st_core.h"
typedef struct
{
    void *ctx;
    st_async_result (*submit)(void *, const uint8_t *, size_t);
    st_async_result (*poll)(void *);
} st_debug_port;
typedef struct
{
    st_debug_port port;
    uint8_t queue[512], tx[64];
    uint16_t head, tail;
    uint32_t window_ms, dropped;
    unsigned window_bytes;
    bool inflight, failed;
} st_debug;
/* Single foreground producer/service owner. Never print or format in ADC ISR.
 * Hardware callbacks only publish completion; no blocking UART calls here. */
void st_debug_init(st_debug *, st_debug_port);
bool st_debug_write(st_debug *, const uint8_t *, size_t, uint32_t now_ms);
void st_debug_service(st_debug *);
#endif
