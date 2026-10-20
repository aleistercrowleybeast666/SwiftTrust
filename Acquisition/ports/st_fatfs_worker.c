#include "st_fatfs_worker.h"
#include <stdio.h>
#include <string.h>
enum
{
    FILE_FREE,
    FILE_QUEUED,
    FILE_RUNNING,
    FILE_DONE,
    FILE_ERROR
};
void st_fatfs_init(st_fatfs_worker *w, bool verified)
{
    memset(w, 0, sizeof(*w));
    atomic_init(&w->state, FILE_FREE);
    w->verified = verified;
}
static st_async_result submit(void *ctx, st_file_op op, const uint8_t *p, size_t n)
{
    st_fatfs_worker *w = ctx;
    if (!w->verified || n > ST_STAGE_BYTES || (n && !p))
        return ST_IO_ERROR;
    if (atomic_load_explicit(&w->state, memory_order_acquire) != FILE_FREE)
        return ST_IO_WAIT;
    w->op = op;
    w->data = p;
    w->length = n;
    w->completed = 0;
    atomic_store_explicit(&w->state, FILE_QUEUED, memory_order_release);
    return ST_IO_DONE;
}
static st_async_result poll(void *ctx, size_t *bytes)
{
    st_fatfs_worker *w = ctx;
    unsigned state = atomic_load_explicit(&w->state, memory_order_acquire);
    if (state == FILE_ERROR)
        return ST_IO_ERROR;
    if (state != FILE_DONE)
        return ST_IO_WAIT;
    *bytes = w->completed;
    atomic_store_explicit(&w->state, FILE_FREE, memory_order_release);
    return ST_IO_DONE;
}
st_file_port st_fatfs_port(st_fatfs_worker *w)
{
    return (st_file_port){w, submit, poll};
}
void st_fatfs_worker_step(st_fatfs_worker *w)
{
    if (atomic_load_explicit(&w->state, memory_order_acquire) != FILE_QUEUED)
        return;
    atomic_store_explicit(&w->state, FILE_RUNNING, memory_order_release);
    FRESULT result = FR_INVALID_OBJECT;
    if (w->op == ST_FILE_OPEN_NEW && !w->opened && w->name_index <= 9999999)
    {
        char name[13];
        (void)snprintf(name, sizeof(name), "S%07lu.BIN", (unsigned long)w->name_index++);
        result = f_open(&w->file, name, FA_WRITE | FA_CREATE_NEW);
        if (result == FR_EXIST && w->name_index <= 9999999)
        {
            /* One collision per task iteration; never truncate or busy-loop. */
            atomic_store_explicit(&w->state, FILE_QUEUED, memory_order_release);
            return;
        }
        if (result == FR_OK)
            w->opened = true;
    }
    else if (w->opened)
    {
        if (w->op == ST_FILE_WRITE)
        {
            UINT actual = 0;
            result = f_write(&w->file, w->data, (UINT)w->length, &actual);
            w->completed = actual;
            if (result == FR_OK && actual != w->length)
                result = FR_DISK_ERR;
        }
        else if (w->op == ST_FILE_SYNC)
            result = f_sync(&w->file);
        else if (w->op == ST_FILE_CLOSE)
        {
            result = f_close(&w->file);
            if (result == FR_OK)
                w->opened = false;
        }
    }
    w->last_result = result;
    atomic_store_explicit(&w->state, result == FR_OK ? FILE_DONE : FILE_ERROR,
                          memory_order_release);
}

bool st_fatfs_idle(const st_fatfs_worker *w)
{
    return w && w->verified && !w->opened &&
           atomic_load_explicit(&w->state, memory_order_acquire) == FILE_FREE;
}
