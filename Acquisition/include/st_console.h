#ifndef ST_CONSOLE_H
#define ST_CONSOLE_H
#include "st_pairing.h"
typedef struct
{
    st_pairing *pair;
    void *ctx;
    void (*emit)(void *, const char *);
    bool (*idle)(void *);
    void (*changed)(void *);
    void (*status)(void *);
    char line[80];
    unsigned used;
    bool overflow;
} st_console;
void st_console_feed(st_console *, uint8_t);
#endif
