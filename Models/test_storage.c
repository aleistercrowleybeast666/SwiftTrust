#include "st_block.h"
#include "st_diskio.h"
#include "st_nv.h"
#include "st_fatfs_worker.h"
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, groups;
#define C(x)                                                                                       \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "storage failed line%d: %s\n", __LINE__, #x);                          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define G(s)                                                                                       \
    do                                                                                             \
    {                                                                                              \
        groups++;                                                                                  \
        puts("PASS " s);                                                                           \
    } while (0)
typedef struct
{
    uint8_t *disk, *pending;
    uint32_t sectors, sector, now;
    unsigned waits, yields, aborts;
    bool present, ready, active, write, error, freeze, never_done, remove_on_yield;
} block_bus;
static bool present(void *ctx)
{
    return ((block_bus *)ctx)->present;
}
static bool ready(void *ctx)
{
    return ((block_bus *)ctx)->ready;
}
static st_async_result submit(void *ctx, bool write, uint8_t *data, uint32_t sector)
{
    block_bus *b = ctx;
    C(!b->active && !((uintptr_t)data & 3u) && sector < b->sectors);
    if (b->error)
        return ST_IO_ERROR;
    b->active = true;
    b->write = write;
    b->pending = data;
    b->sector = sector;
    b->waits = 1;
    return ST_IO_DONE;
}
static st_async_result poll(void *ctx)
{
    block_bus *b = ctx;
    C(b->active);
    if (b->error)
        return ST_IO_ERROR;
    if (b->never_done || b->waits-- != 0)
        return ST_IO_WAIT;
    if (b->write)
        memcpy(b->disk + (size_t)b->sector * 512, b->pending, 512);
    else
        memcpy(b->pending, b->disk + (size_t)b->sector * 512, 512);
    b->active = false;
    return ST_IO_DONE;
}
static void abort_io(void *ctx)
{
    block_bus *b = ctx;
    b->aborts++;
    b->active = false;
}
static uint32_t now_ms(void *ctx)
{
    return ((block_bus *)ctx)->now;
}
static void yield_task(void *ctx)
{
    block_bus *b = ctx;
    b->yields++;
    if (!b->freeze)
        b->now++;
    if (b->remove_on_yield)
        b->present = false;
}
static void bind_block(st_block_device *d, block_bus *b)
{
    st_block_bind(d, (st_block_port){b, present, ready, submit, poll, abort_io, now_ms, yield_task},
                  b->sectors, 0, 20, true);
    C(st_block_initialize(d));
}
static void block_tests(void)
{
    uint8_t disk[2048] = {0}, unaligned[1025], readback[1025];
    for (unsigned i = 0; i < 1024; i++)
        unaligned[i + 1] = (uint8_t)(i * 17u);
    block_bus b = {.disk = disk, .sectors = 4, .present = true, .ready = true};
    st_block_device d;
    bind_block(&d, &b);
    C(st_block_transfer(&d, true, 1, 2, unaligned + 1) == ST_BLOCK_OK);
    C(!memcmp(disk + 512, unaligned + 1, 1024));
    C(st_block_transfer(&d, false, 1, 2, readback + 1) == ST_BLOCK_OK);
    C(!memcmp(readback + 1, unaligned + 1, 1024) && b.yields == 4);
    C(st_block_transfer(&d, false, 3, 2, readback) == ST_BLOCK_PARAMETER);
    C(st_block_transfer(&d, false, UINT32_MAX, 1, readback) == ST_BLOCK_PARAMETER);
    C(st_block_transfer(&d, false, 0, 0, readback) == ST_BLOCK_PARAMETER);
    b.never_done = true;
    b.now = UINT32_MAX - 5;
    C(st_block_transfer(&d, false, 0, 1, readback) == ST_BLOCK_ERROR && d.fault && b.aborts == 1);
    C(st_block_transfer(&d, false, 0, 1, readback) == ST_BLOCK_NOT_READY);
    b.never_done = false;
    b.now = 0;
    bind_block(&d, &b);
    b.remove_on_yield = true;
    C(st_block_transfer(&d, false, 0, 1, readback) == ST_BLOCK_ERROR && d.fault && !b.active);
    b.present = true;
    b.remove_on_yield = false;
    C(!st_block_initialize(&d));
    bind_block(&d, &b);
    b.freeze = true;
    b.never_done = true;
    C(st_block_transfer(&d, false, 0, 1, readback) == ST_BLOCK_ERROR && b.yields < 5000);
    G("block DMA-aligned retained bounce / unaligned caller / two-sector roundtrip / bounds / wrap "
      "timeout / removal / frozen tick / latched failure");
}
static void diskio_tests(void)
{
    uint8_t disk[2048] = {0}, data[512] = {0};
    block_bus b = {.disk = disk, .sectors = 4, .present = true, .ready = true};
    st_block_device d;
    bind_block(&d, &b);
    st_diskio_bind(NULL);
    C(disk_initialize(0) & STA_NOINIT);
    C(disk_read(0, data, 0, 1) == RES_NOTRDY);
    st_diskio_bind(&d);
    C(disk_initialize(0) == 0 && disk_status(0) == 0);
    C(disk_initialize(1) & STA_NOINIT);
    DWORD sectors = 0;
    WORD size = 0;
    C(disk_ioctl(0, GET_SECTOR_COUNT, &sectors) == RES_OK && sectors == 4);
    C(disk_ioctl(0, GET_SECTOR_SIZE, &size) == RES_OK && size == 512);
    C(disk_ioctl(0, GET_BLOCK_SIZE, &sectors) == RES_PARERR);
    C(disk_write(0, data, 0, 1) == RES_OK && disk_read(0, data, 0, 1) == RES_OK &&
      disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK);
    b.present = false;
    C(disk_status(0) & STA_NODISK);
    C(disk_read(0, data, 0, 1) == RES_NOTRDY);
    st_diskio_bind(NULL);
    G("real FatFs diskio interface / unbound and wrong drive / geometry / unknown erase size / "
      "read-write-sync / hot removal");
}
static void make_fat16(uint8_t *disk)
{
    disk[0] = 0xeb;
    disk[1] = 0x3c;
    disk[2] = 0x90;
    memcpy(disk + 3, "STTEST  ", 8);
    st_put16(disk + 11, 512);
    disk[13] = 1;
    st_put16(disk + 14, 1);
    disk[16] = 2;
    st_put16(disk + 17, 64);
    st_put16(disk + 19, 8192);
    disk[21] = 0xf8;
    st_put16(disk + 22, 32);
    st_put16(disk + 24, 32);
    st_put16(disk + 26, 64);
    disk[36] = 0x80;
    disk[38] = 0x29;
    st_put32(disk + 39, 123);
    memcpy(disk + 43, "SYNTHETIC  ", 11);
    memcpy(disk + 54, "FAT16   ", 8);
    disk[510] = 0x55;
    disk[511] = 0xaa;
    for (unsigned fat = 0; fat < 2; fat++)
    {
        size_t at = (1u + fat * 32u) * 512u;
        disk[at] = 0xf8;
        disk[at + 1] = disk[at + 2] = disk[at + 3] = 0xff;
    }
}
static void fatfs_logger_test(void)
{
    uint8_t *disk = calloc(8192, 512);
    C(disk != NULL);
    make_fat16(disk);
    block_bus bus = {.disk = disk, .sectors = 8192, .present = true, .ready = true};
    st_block_device device;
    bind_block(&device, &bus);
    device.timeout_ms = 1000;
    st_diskio_bind(&device);
    FATFS fs;
    C(f_mount(&fs, "", 1) == FR_OK);
    st_fatfs_worker worker;
    st_fatfs_init(&worker, true);
    st_log_config cfg = {.adc_clock_hz = 8192000,
                         .osr = 2048,
                         .rate_millihz = 2000000,
                         .boot_epoch = 1,
                         .gain_ch0 = 1,
                         .gain_ch1 = 1,
                         .clock_verified = true,
                         .simulated = true};
    st_logger *logger = malloc(sizeof(*logger));
    C(logger != NULL);
    st_logger_init(logger, cfg, st_fatfs_port(&worker));
    C(st_logger_start(logger, 0));
    uint64_t now = 0;
    for (unsigned i = 0; i < 50 && logger->state != ST_CAPTURING; i++)
    {
        st_logger_service(logger, now);
        st_fatfs_worker_step(&worker);
        now += 1000;
    }
    C(logger->state == ST_CAPTURING);
    for (uint32_t i = 1; i <= 1500; i++)
    {
        st_capture capture = {.time_us = now, .sequence = i};
        capture.bytes[0] = 0x01;
        capture.bytes[1] = 0x03;
        st_put16(capture.bytes + 9, 0);
        uint16_t crc = st_crc16(capture.bytes, 9);
        capture.bytes[9] = (uint8_t)(crc >> 8);
        capture.bytes[10] = (uint8_t)crc;
        C(st_logger_offer_isr(logger, &capture));
        st_logger_service(logger, now);
        st_fatfs_worker_step(&worker);
        now += 500;
    }
    C(st_logger_stop(logger));
    for (unsigned i = 0; i < 1000 && logger->state != ST_IDLE; i++)
    {
        st_logger_service(logger, now);
        st_fatfs_worker_step(&worker);
        now += 1000;
    }
    C(logger->state == ST_IDLE && logger->have_durable && !worker.opened &&
      st_logger_error_count(logger) == 0);
    FIL file;
    C(f_open(&file, "S0000000.BIN", FA_READ) == FR_OK);
    C(f_size(&file) == 512u + 1501u * 32u);
    uint8_t header[512], record[32];
    UINT got = 0;
    C(f_read(&file, header, 512, &got) == FR_OK && got == 512 && !memcmp(header, "STLOG001", 8));
    for (unsigned i = 0; i < 1501; i++)
    {
        C(f_read(&file, record, 32, &got) == FR_OK && got == 32);
        C(st_get32(record + 28) == st_crc32(record, 28));
        if (i < 1500)
            C(st_get32(record) == i + 1);
        else
            C(st_get16(record + 22) == ST_REC_END);
    }
    C(f_close(&file) == FR_OK);
    C(f_mount(NULL, "", 0) == FR_OK);
    st_diskio_bind(NULL);
    free(logger);
    free(disk);
    G("unmodified FatFs R0.12c + real worker + logger + diskio on synthetic RAM FAT16 / 1500 "
      "records / file size / per-record CRC / END / durable close (no physical SD)");
}
typedef struct
{
    uint8_t slots[2][96];
    unsigned writes, cut;
    bool fail_write, fail_read, unverified;
} nv_bus;
static bool nv_read(void *ctx, unsigned slot, uint8_t out[96])
{
    nv_bus *b = ctx;
    C(slot < 2);
    if (b->fail_read)
        return false;
    memcpy(out, b->slots[slot], 96);
    return true;
}
static bool nv_write(void *ctx, unsigned slot, const uint8_t in[96])
{
    nv_bus *b = ctx;
    C(slot < 2);
    b->writes++;
    memset(b->slots[slot], 255, 96);
    memcpy(b->slots[slot], in, b->cut);
    return !b->fail_write;
}
static st_nv_identity identity(uint8_t role)
{
    st_nv_identity id = {.own_uid = {1}, .peer_uid = {2}, .role = role};
    if (role == ST_NV_ACQUISITION)
    {
        id.epoch_first = 100;
        id.epoch_last = 102;
    }
    return id;
}
static void nv_bind(st_nv_store *s, nv_bus *b, uint8_t role)
{
    st_nv_bind(s, (st_nv_storage){b, nv_read, nv_write, !b->unverified}, identity(role));
}
static void nv_reset(nv_bus *b)
{
    memset(b, 0, sizeof(*b));
    memset(b->slots, 255, sizeof(b->slots));
    b->cut = 96;
}
static void nv_tests(void)
{
    nv_bus b;
    nv_reset(&b);
    st_nv_store s;
    nv_bind(&s, &b, ST_NV_CONTROLLER);
    C(st_nv_open(&s) == ST_NV_EMPTY);
    uint64_t counter = 0;
    st2_nv_port port = st_nv_link_port(&s);
    C(!port.reserve_counter(port.ctx, &counter));
    C(st_nv_provision(&s) && !st_nv_provision(&s));
    C(port.reserve_counter(port.ctx, &counter) && counter == 1);
    nv_bind(&s, &b, ST_NV_CONTROLLER);
    C(st_nv_open(&s) == ST_NV_VALID && s.values.counter == 1);
    port = st_nv_link_port(&s);
    C(port.reserve_counter(port.ctx, &counter) && counter == 2);
    st_nv_identity foreign = identity(ST_NV_CONTROLLER);
    foreign.peer_uid[1] = 3;
    st_nv_bind(&s, (st_nv_storage){&b, nv_read, nv_write, true}, foreign);
    C(st_nv_open(&s) == ST_NV_FOREIGN && !st_nv_provision(&s));
    nv_reset(&b);
    nv_bind(&s, &b, ST_NV_ACQUISITION);
    C(st_nv_provision(&s));
    port = st_nv_link_port(&s);
    uint32_t epoch = 0;
    C(port.reserve_epoch(port.ctx, 7, &epoch) && epoch == 100);
    C(!port.reserve_epoch(port.ctx, 7, &epoch));
    C(port.reserve_epoch(port.ctx, 8, &epoch) && epoch == 101);
    C(port.reserve_epoch(port.ctx, 9, &epoch) && epoch == 102);
    C(!port.reserve_epoch(port.ctx, 10, &epoch) && s.status == ST_NV_EXHAUSTED);
    G("NV explicit blank provisioning / alternating slots / reopened counter / paired identity "
      "mismatch / monotonic peer counter / assigned epoch range exhaustion");
}
static void nv_powercuts(void)
{
    for (unsigned cut = 0; cut <= 96; cut++)
    {
        nv_bus b;
        nv_reset(&b);
        st_nv_store s;
        nv_bind(&s, &b, ST_NV_CONTROLLER);
        C(st_nv_provision(&s));
        b.cut = cut;
        b.fail_write = true;
        st2_nv_port p = st_nv_link_port(&s);
        uint64_t out = 999;
        C(!p.reserve_counter(p.ctx, &out) && out == 999);
        b.fail_write = false;
        b.cut = 96;
        nv_bind(&s, &b, ST_NV_CONTROLLER);
        st_nv_status state = st_nv_open(&s);
        if (cut == 0)
            C(state == ST_NV_VALID && s.values.counter == 0);
        else if (cut == 96)
        {
            C(state == ST_NV_VALID && s.values.counter == 1);
            p = st_nv_link_port(&s);
            C(p.reserve_counter(p.ctx, &out) && out == 2);
        }
        else
            C(state == ST_NV_DAMAGED && !st_nv_provision(&s));
    }
    nv_bus b;
    nv_reset(&b);
    b.unverified = true;
    st_nv_store s;
    nv_bind(&s, &b, ST_NV_CONTROLLER);
    C(st_nv_open(&s) == ST_NV_UNBOUND && b.writes == 0);
    b.unverified = false;
    nv_bind(&s, &b, ST_NV_CONTROLLER);
    C(st_nv_provision(&s));
    b.slots[0][35] ^= 1;
    C(st_nv_open(&s) == ST_NV_DAMAGED && !st_nv_provision(&s));
    b.fail_read = true;
    C(st_nv_open(&s) == ST_NV_IO_ERROR);
    G("NV simulated cut at all97 byte boundaries / no automatic rollback from damaged programmed "
      "slot / committed-but-error skips used counter / bad CRC / unverified storage / read "
      "failure");
}
unsigned st_test_storage(unsigned *out)
{
    block_tests();
    diskio_tests();
    fatfs_logger_test();
    nv_tests();
    nv_powercuts();
    *out += groups;
    return checks;
}
