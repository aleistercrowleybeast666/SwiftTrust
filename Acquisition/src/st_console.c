#include "st_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static void command(st_console *c)
{
    st_pairing *p = c->pair;
    if (!strcmp(c->line, "INFO"))
    {
        char out[100], uid[25];
        for (unsigned i = 0; i < 12; i++)
            (void)snprintf(uid + 2 * i, 3, "%02X", p->own_uid[i]);
        (void)snprintf(out, sizeof out, "UID %s ROLE %u NV %u\r\n", uid, p->role,
                       (unsigned)p->store.status);
        c->emit(c->ctx, out);
        return;
    }
    if (!strcmp(c->line, "STATUS") && c->status)
    {
        c->status(c->ctx);
        return;
    }
    if (!strcmp(c->line, "NVINIT NEW"))
    {
        bool ok = st_pairing_initialize_new(p, c->idle(c->ctx), ST_PAIR_NEW_MEDIA_CONFIRM);
        c->emit(c->ctx, ok ? "NV EMPTY\r\n" : "NVINIT DENIED; PRESERVE HISTORY\r\n");
        return;
    }
    if (!strncmp(c->line, "PAIR ", 5) && strlen(c->line) >= 31 && c->line[29] == ' ')
    {
        uint8_t peer[12];
        bool ok = true;
        for (unsigned i = 0; i < 12; i++)
        {
            int a = hex(c->line[5 + 2 * i]), b = hex(c->line[6 + 2 * i]);
            if (a < 0 || b < 0)
                ok = false;
            peer[i] = (uint8_t)((a < 0 ? 0 : a) * 16 + (b < 0 ? 0 : b));
        }
        const char *digits = c->line + 30;
        char *end = NULL;
        unsigned long net = strtoul(digits, &end, 10);
        for (const char *s = digits; *s; s++)
            if (*s < '0' || *s > '9')
                ok = false;
        if (end == digits || *end || net > 254)
            ok = false;
        ok = ok && st_pairing_provision(p, peer, (unsigned)net, c->idle(c->ctx));
        c->emit(c->ctx, ok ? "PAIR SAVED\r\n" : "PAIR DENIED\r\n");
        if (ok && c->changed)
            c->changed(c->ctx);
        return;
    }
    c->emit(c->ctx, "INFO | STATUS | PAIR <24hexUID> <net> | NVINIT NEW\r\n");
}
void st_console_feed(st_console *c, uint8_t byte)
{
    if (byte == '\r')
        return;
    if (byte == '\n')
    {
        c->line[c->used] = 0;
        if (c->overflow)
            c->emit(c->ctx, "LINE TOO LONG\r\n");
        else if (c->used)
            command(c);
        c->used = 0;
        c->overflow = false;
        return;
    }
    if (byte < 32 || byte > 126)
    {
        c->overflow = true;
        return;
    }
    if (c->used + 1 >= sizeof c->line)
    {
        c->overflow = true;
        return;
    }
    c->line[c->used++] = (char)byte;
}
