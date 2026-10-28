#include "st_cal_fatfs.h"
#include <limits.h>
#include <string.h>
static const char *const names[] = {"CAL0.BIN", "CAL1.BIN"};
static bool idle(st_cal_fatfs *s)
{
    return s->logger && s->logger->state == ST_IDLE && !s->logger->inflight &&
           !atomic_load_explicit(&s->logger->capture_enabled, memory_order_acquire) &&
           st_fatfs_idle(s->worker);
}
void st_cal_fatfs_init(st_cal_fatfs *s, st_fatfs_worker *w, st_logger *l, st_cal_binding b)
{
    *s = (st_cal_fatfs){.worker = w, .logger = l, .binding = b};
}
static bool parse(const uint8_t *p, st_cal_profile *out, uint64_t *gen)
{
    if (memcmp(p, "STCS0001", 8) || memcmp(p + 512, "STCC0001", 8) || !st_get64(p + 8) ||
        st_get32(p + 508) != st_crc32(p, 508) || st_get32(p + 1020) != st_crc32(p + 512, 508) ||
        st_get64(p + 8) != st_get64(p + 520) || st_get32(p + 528) != st_get32(p + 508))
        return false;
    for (unsigned i = 16; i < 32; i++)
        if (p[i])
            return false;
    for (unsigned i = 288; i < 508; i++)
        if (p[i])
            return false;
    for (unsigned i = 532; i < 1020; i++)
        if (p[i])
            return false;
    if (!st_cal_profile_decode(out, p + 32, 256))
        return false;
    *gen = st_get64(p + 8);
    return true;
}
/* 0=absent,1=valid,2=damaged,-1=IO. Never expose a partial decode. */
static int read_slot(st_cal_fatfs *s, unsigned slot, st_cal_profile *out, uint64_t *gen)
{
    FIL file;
    UINT got = 0;
    FRESULT r = f_open(&file, names[slot], FA_READ);
    s->last_result = r;
    if (r == FR_NO_FILE)
        return 0;
    if (r != FR_OK)
        return -1;
    bool size = f_size(&file) == 1024;
    if (size)
        r = f_read(&file, s->frame, 1024, &got);
    FRESULT close = f_close(&file);
    if (r != FR_OK || close != FR_OK)
    {
        s->last_result = r != FR_OK ? r : close;
        return -1;
    }
    if (!size || got != 1024 || !parse(s->frame, out, gen))
        return 2;
    return 1;
}
st_cal_disk_state st_cal_fatfs_open(st_cal_fatfs *s, st_cal_profile *active)
{
    if (!active || !idle(s))
    {
        s->state = ST_CAL_DISK_BUSY;
        return s->state;
    }
    s->opened = false;
    st_cal_profile best = {0}, p;
    uint64_t best_gen = 0, gen = 0;
    uint64_t foreign_gen = 0, previous_gen = 0;
    bool damaged = false;
    unsigned best_slot = 0;
    unsigned foreign_slot = 0;
    for (unsigned i = 0; i < 2; i++)
    {
        int r = read_slot(s, i, &p, &gen);
        if (r < 0)
        {
            s->state = ST_CAL_DISK_IO;
            s->fault = true;
            return s->state;
        }
        if (r == 2)
            damaged = true;
        if (r != 1)
            continue;
        if (gen == previous_gen)
        {
            s->state = ST_CAL_DISK_DAMAGED;
            s->fault = true;
            return s->state;
        }
        previous_gen = gen;
        if (!st_cal_binding_equal(p.binding, s->binding))
        {
            if (gen > foreign_gen)
            {
                foreign_gen = gen;
                foreign_slot = i;
            }
            continue;
        }
        if (gen == best_gen)
        {
            s->state = ST_CAL_DISK_DAMAGED;
            s->fault = true;
            return s->state;
        }
        if (gen > best_gen)
        {
            best = p;
            best_gen = gen;
            best_slot = i;
        }
    }
    if (!best_gen && !foreign_gen && damaged)
    {
        s->state = ST_CAL_DISK_DAMAGED;
        s->fault = true;
        return s->state;
    }
    /* A newer incompatible configuration must invalidate even an older matching
     * slot. Only an explicit completed calibration may create the next generation. */
    if (foreign_gen > best_gen)
    {
        s->generation = foreign_gen;
        s->revision = 0;
        s->active_slot = foreign_slot;
        s->opened = true;
        s->fault = false;
        s->state = ST_CAL_DISK_MIGRATION;
        *active = (st_cal_profile){0};
        return s->state;
    }
    s->generation = best_gen;
    s->revision = best_gen ? best.revision : 0;
    s->active_slot = best_slot;
    s->opened = true;
    s->fault = false;
    s->state = best_gen ? ST_CAL_DISK_VALID : ST_CAL_DISK_EMPTY;
    if (best_gen)
        *active = best;
    return s->state;
}
static bool save(void *ctx, const uint8_t profile[256])
{
    st_cal_fatfs *s = ctx;
    st_cal_profile p;
    if (!s->opened || s->fault || !idle(s) || s->generation == UINT64_MAX ||
        !st_cal_profile_decode(&p, profile, 256) || !st_cal_binding_equal(p.binding, s->binding) ||
        s->revision == UINT32_MAX || p.revision != s->revision + 1)
        return false;
    unsigned slot = s->generation ? 1u - s->active_slot : 0;
    uint64_t gen = s->generation + 1;
    memset(s->frame, 0, sizeof(s->frame));
    memcpy(s->frame, "STCS0001", 8);
    st_put64(s->frame + 8, gen);
    memcpy(s->frame + 32, profile, 256);
    st_put32(s->frame + 508, st_crc32(s->frame, 508));
    memcpy(s->frame + 512, "STCC0001", 8);
    st_put64(s->frame + 520, gen);
    st_put32(s->frame + 528, st_get32(s->frame + 508));
    st_put32(s->frame + 1020, st_crc32(s->frame + 512, 508));
    FIL file;
    UINT n = 0;
    FRESULT r = f_open(&file, names[slot], FA_WRITE | FA_OPEN_ALWAYS);
    bool opened = r == FR_OK;
    if (opened && f_size(&file) != 0 && f_size(&file) != 1024)
        r = FR_INVALID_OBJECT;
    if (r == FR_OK)
    {
        r = f_write(&file, s->frame, 512, &n);
        if (r == FR_OK && n != 512)
            r = FR_DISK_ERR;
    }
    if (r == FR_OK)
        r = f_sync(&file); /* prepare before commit seal */
    if (r == FR_OK)
    {
        r = f_write(&file, s->frame + 512, 512, &n);
        if (r == FR_OK && n != 512)
            r = FR_DISK_ERR;
    }
    if (r == FR_OK)
        r = f_sync(&file);
    if (opened)
    {
        FRESULT close = f_close(&file);
        if (r == FR_OK)
            r = close;
    }
    /* Do not return true on queued IO, sync alone, close alone, or cached RAM. */
    if (r == FR_OK)
    {
        r = f_open(&file, names[slot], FA_READ);
        opened = r == FR_OK;
        if (opened && f_size(&file) != 1024)
            r = FR_INVALID_OBJECT;
        uint8_t check[512];
        for (unsigned i = 0; i < 2 && r == FR_OK; i++)
        {
            r = f_read(&file, check, 512, &n);
            if (r == FR_OK && (n != 512 || memcmp(check, s->frame + i * 512, 512)))
                r = FR_DISK_ERR;
        }
        if (opened)
        {
            FRESULT close = f_close(&file);
            if (r == FR_OK)
                r = close;
        }
    }
    s->last_result = r;
    if (r != FR_OK)
    {
        s->fault = true;
        s->state = ST_CAL_DISK_IO;
        return false;
    }
    s->generation = gen;
    s->revision = p.revision;
    s->active_slot = slot;
    s->state = ST_CAL_DISK_VALID;
    return true;
}
st_cal_storage st_cal_fatfs_storage(st_cal_fatfs *s)
{
    return (st_cal_storage){s, save};
}
