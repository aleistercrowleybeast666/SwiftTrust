#include "st_block.h"
#include "st_diskio.h"
#include "st_nv.h"
#include "st_fatfs_worker.h"
#include "st_cal_system.h"
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
    unsigned operations, fail_at, tear_bytes, delay;
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
    if (++b->operations == b->fail_at)
    {
        if (write && b->tear_bytes)
            memcpy(b->disk + (size_t)sector * 512, data, b->tear_bytes);
        return ST_IO_ERROR;
    }
    if (b->error)
        return ST_IO_ERROR;
    b->active = true;
    b->write = write;
    b->pending = data;
    b->sector = sector;
    b->waits = b->delay ? b->delay : 1;
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
static st_logger *cooperative_logger;
static uint32_t cooperative_sequence, cooperative_target;
static void yield_task(void *ctx)
{
    block_bus *b = ctx;
    b->yields++;
    if (!b->freeze)
        b->now++;
    if (b->remove_on_yield)
        b->present = false;
    if (cooperative_logger)
    {
        for (unsigned i = 0; i < 2 && cooperative_sequence < cooperative_target; i++)
        {
            if (!atomic_load(&cooperative_logger->capture_enabled))
                break;
            st_capture capture = {.sequence = ++cooperative_sequence,
                                  .time_us = (uint64_t)b->now * 1000u - 500u + (uint64_t)i * 500u};
            C(st_logger_offer_isr(cooperative_logger, &capture));
        }
        st_logger_service(cooperative_logger, (uint64_t)b->now * 1000u);
    }
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
    st_log_config cfg = {.adc_clock_hz = ST_ADC_CLOCK_HZ,
                         .osr = ST_ADC_OSR,
                         .rate_millihz = ST_ADC_RATE_MILLIHZ,
                         .boot_epoch = 1,
                         .gain_ch0 = 1,
                         .gain_ch1 = 0,
                         .clock_verified = true,
                         .simulated = true};
    st_logger *logger = malloc(sizeof(*logger));
    C(logger != NULL);
    Log_ApplyAds1220Metadata(&cfg, false, 0);
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
static void cooperative_storage_test(void)
{
    uint8_t *disk = calloc(8192, 512);
    C(disk);
    make_fat16(disk);
    block_bus bus = {.disk = disk, .sectors = 8192, .present = true, .ready = true, .delay = 2};
    st_block_device device;
    bind_block(&device, &bus);
    device.timeout_ms = 250;
    st_diskio_bind(&device);
    FATFS fs;
    C(f_mount(&fs, "", 1) == FR_OK);
    st_fatfs_worker worker;
    st_fatfs_init(&worker, true);
    st_logger *l = malloc(sizeof *l);
    C(l);
    st_log_config config = {.adc_clock_hz = ST_ADC_CLOCK_HZ,
                            .osr = ST_ADC_OSR,
                            .rate_millihz = ST_ADC_RATE_MILLIHZ,
                            .boot_epoch = 1,
                            .gain_ch0 = 1,
                            .gain_ch1 = 0,
                            .clock_verified = true,
                            .simulated = true};
    Log_ApplyAds1220Metadata(&config, false, 0);
    st_logger_init(l, config, st_fatfs_port(&worker));
    C(st_logger_start(l, (uint64_t)bus.now * 1000));
    cooperative_logger = l;
    cooperative_sequence = 0;
    cooperative_target = 3000;
    for (unsigned i = 0; i < 10000; i++)
    {
        yield_task(&bus); /* Timer/ADC progression outside or inside a blocking sector. */
        if (cooperative_sequence >= cooperative_target)
            C(st_logger_stop(l));
        st_logger_service(l, (uint64_t)bus.now * 1000);
        st_fatfs_worker_step(&worker);
        if (cooperative_sequence == cooperative_target && l->state == ST_IDLE)
            break;
    }
    cooperative_logger = NULL;
    C(cooperative_sequence == 3000 && l->state == ST_IDLE);
    C(!atomic_load(&l->ring.dropped) && !st_logger_error_count(l) && l->have_durable);
    FIL file;
    C(f_open(&file, "S0000000.BIN", FA_READ) == FR_OK);
    C(f_size(&file) == 512u + 3001u * 32u);
    C(f_lseek(&file, 512) == FR_OK);
    uint8_t record[32];
    UINT got;
    for (unsigned i = 1; i <= 3001; i++)
    {
        C(f_read(&file, record, 32, &got) == FR_OK && got == 32);
        C(st_get32(record + 28) == st_crc32(record, 28));
        if (i <= 3000)
            C(st_get32(record) == i && !st_get16(record + 22));
        else
            C(st_get16(record + 22) == ST_REC_END);
    }
    C(f_close(&file) == FR_OK);
    C(f_mount(NULL, "", 0) == FR_OK);
    st_diskio_bind(NULL);
    free(l);
    free(disk);
    G("cooperative real FatFs/worker/diskio: 2kHz model continues in sector waits, logger service "
      "never reenters FatFs, 3000 records without drop, clean STOP");
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
    b.slots[0][ST2_MAX] ^= 1;
    C(st_nv_open(&s) == ST_NV_DAMAGED && !st_nv_provision(&s));
    b.fail_read = true;
    C(st_nv_open(&s) == ST_NV_IO_ERROR);
    G("NV simulated cut at all97 byte boundaries / no automatic rollback from damaged programmed "
      "slot / committed-but-error skips used counter / bad CRC / unverified storage / read "
      "failure");
}

st_cal_profile st_cal_test_profile(void);
static void read_cal_file(const char *name, uint8_t out[1024])
{
    FIL f;
    UINT n;
    C(f_open(&f, name, FA_READ) == FR_OK);
    C(f_size(&f) == 1024);
    C(f_read(&f, out, 1024, &n) == FR_OK && n == 1024);
    C(f_close(&f) == FR_OK);
}
static void cal_fatfs_test(void)
{
    uint8_t *disk = calloc(8192, 512), *base = malloc(8192u * 512u);
    C(disk && base);
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
    st_logger *logger = malloc(sizeof(*logger));
    st_cal_system *system = malloc(sizeof(*system));
    C(logger && system);
    st_cal_profile p = st_cal_test_profile(), loaded = {0};
    uint8_t encoded[256], old_file[1024], again[1024];
    st_log_config cfg = {0};
    Log_ApplyAds1220Metadata(&cfg, false, 0);
    st_logger_init(logger, cfg, st_fatfs_port(&worker));
    C(st_cal_system_init(system, logger, &worker, NULL, p.binding, p.policy, NULL, NULL) ==
      ST_CAL_DISK_EMPTY);
    st_cal_storage port = st_cal_fatfs_storage(&system->disk);
    C(st_cal_profile_encode(&p, encoded) == 256 && port.save(port.ctx, encoded));
    system->active = p;
    p.revision = 2;
    C(st_cal_profile_encode(&p, encoded) == 256 && port.save(port.ctx, encoded));
    system->active = p;
    C(system->disk.generation == 2 && system->disk.active_slot == 1);
    unsigned no_io = bus.operations;
    C(!port.save(port.ctx, encoded) &&
      no_io == bus.operations); /* stale revision cannot overwrite */
    C(st_cal_fatfs_open(&system->disk, &loaded) == ST_CAL_DISK_VALID && loaded.revision == 2);
    read_cal_file("CAL1.BIN", old_file);
    C(f_mount(NULL, "", 0) == FR_OK);
    memcpy(base, disk, 8192u * 512u);
    C(f_mount(&fs, "", 1) == FR_OK);
    p.revision = 3;
    C(st_cal_profile_encode(&p, encoded) == 256);
    logger->state = ST_CAPTURING;
    unsigned before = bus.operations;
    C(!port.save(port.ctx, encoded) && bus.operations == before && system->active.revision == 2);
    logger->state = ST_IDLE;
    before = bus.operations;
    C(port.save(port.ctx, encoded));
    unsigned count = bus.operations - before;
    C(count > 2 && count < 100);
    /* Repeat every physical sector submit, including readback. Existing fixed
     * slots avoid allocator changes in this fault model. A real card FTL may do
     * more than the model; this is not a physical brownout guarantee. */
    unsigned failures = 0, uncertain = 0;
    for (unsigned tear = 0; tear < 3; tear++)
        for (unsigned cut = 1; cut <= count; cut++)
        {
            C(f_mount(NULL, "", 0) == FR_OK);
            memcpy(disk, base, 8192u * 512u);
            bus.fail_at = 0;
            bus.operations = 0;
            bus.active = false;
            bus.tear_bytes = tear == 0 ? 0 : (tear == 1 ? 17 : 511);
            bind_block(&device, &bus);
            device.timeout_ms = 1000;
            st_diskio_bind(&device);
            C(f_mount(&fs, "", 1) == FR_OK);
            st_cal_fatfs_init(&system->disk, &worker, logger, p.binding);
            C(st_cal_fatfs_open(&system->disk, &system->active) == ST_CAL_DISK_VALID &&
              system->active.revision == 2);
            port = st_cal_fatfs_storage(&system->disk);
            bus.fail_at = bus.operations + cut;
            bool ok = port.save(port.ctx, encoded);
            C(system->active.revision == 2);
            if (!ok)
            {
                failures++;
                C(system->disk.fault);
                before = bus.operations;
                C(!port.save(port.ctx, encoded) && before == bus.operations);
            }
            bus.fail_at = 0;
            C(f_mount(NULL, "", 0) == FR_OK);
            bind_block(&device, &bus);
            device.timeout_ms = 1000;
            st_diskio_bind(&device);
            C(f_mount(&fs, "", 1) == FR_OK);
            read_cal_file("CAL1.BIN", again);
            C(!memcmp(old_file, again, 1024));
            st_cal_fatfs_init(&system->disk, &worker, logger, p.binding);
            C(st_cal_fatfs_open(&system->disk, &loaded) == ST_CAL_DISK_VALID);
            C(loaded.revision == 2 || loaded.revision == 3);
            if (!ok && loaded.revision == 3)
                uncertain++;
        }
    C(failures && uncertain);
    /* A changed sensor/config cannot load old coefficients. Explicitly completed
     * calibration can rebind safely, preserving the newest foreign slot. */
    st_cal_binding foreign = p.binding;
    foreign.sensor_id++;
    st_cal_fatfs_init(&system->disk, &worker, logger, foreign);
    loaded.revision = 99;
    C(st_cal_fatfs_open(&system->disk, &loaded) == ST_CAL_DISK_MIGRATION && !loaded.valid);
    C(!st_cal_fatfs_storage(&system->disk).save(&system->disk, encoded));
    unsigned retained_slot = system->disk.active_slot;
    const char *retained_name = retained_slot ? "CAL1.BIN" : "CAL0.BIN";
    read_cal_file(retained_name, old_file);
    st_cal_profile rebound = st_cal_test_profile();
    rebound.binding = foreign;
    C(st_cal_profile_encode(&rebound, encoded) == 256);
    C(st_cal_fatfs_storage(&system->disk).save(&system->disk, encoded));
    read_cal_file(retained_name, again);
    C(!memcmp(old_file, again, 1024));
    st_cal_fatfs_init(&system->disk, &worker, logger, foreign);
    C(st_cal_fatfs_open(&system->disk, &loaded) == ST_CAL_DISK_VALID && loaded.revision == 1 &&
      loaded.binding.sensor_id == foreign.sensor_id);
    st_cal_fatfs_init(&system->disk, &worker, logger, p.binding);
    C(st_cal_fatfs_open(&system->disk, &loaded) == ST_CAL_DISK_MIGRATION && !loaded.valid);
    C(f_mount(NULL, "", 0) == FR_OK);
    /* A legacy ADC binding is recognized without applying its coefficients.
     * Migration preserves the newest old file until a new profile is durable. */
    memset(disk, 0, 8192u * 512u);
    make_fat16(disk);
    bind_block(&device, &bus);
    device.timeout_ms = 1000;
    st_diskio_bind(&device);
    C(f_mount(&fs, "", 1) == FR_OK);
    st_cal_profile legacy = st_cal_test_profile();
    legacy.binding.adc_model = 0;
    legacy.binding.reference_uv = 0;
    legacy.binding.input_mux = 0;
    legacy.binding.pga_bypass = false;
    legacy.binding.adc_clock_hz = 8192000;
    legacy.binding.osr = 2048;
    legacy.binding.rate_millihz = 2000000;
    legacy.binding.gain1 = 1;
    legacy.binding.excitation_nominal_uv = 2500000;
    st_cal_fatfs_init(&system->disk, &worker, logger, legacy.binding);
    C(st_cal_fatfs_open(&system->disk, &loaded) == ST_CAL_DISK_EMPTY);
    port = st_cal_fatfs_storage(&system->disk);
    C(st_cal_profile_encode(&legacy, encoded) == 256 && port.save(port.ctx, encoded));
    legacy.revision = 2;
    C(st_cal_profile_encode(&legacy, encoded) == 256 && port.save(port.ctx, encoded));
    read_cal_file("CAL1.BIN", old_file);
    p = st_cal_test_profile();
    C(st_cal_system_init(system, logger, &worker, NULL, p.binding, p.policy, NULL, NULL) ==
      ST_CAL_DISK_MIGRATION);
    C(!system->active.valid && !logger->config.calibration_valid && system->service.storage.save);
    port = st_cal_fatfs_storage(&system->disk);
    C(st_cal_profile_encode(&p, encoded) == 256 && port.save(port.ctx, encoded));
    C(system->disk.generation == 3 && system->disk.active_slot == 0);
    read_cal_file("CAL1.BIN", again);
    C(!memcmp(old_file, again, 1024));
    st_cal_fatfs_init(&system->disk, &worker, logger, p.binding);
    C(st_cal_fatfs_open(&system->disk, &loaded) == ST_CAL_DISK_VALID && loaded.revision == 1 &&
      loaded.binding.adc_model == 1220);
    C(f_mount(NULL, "", 0) == FR_OK);
    st_diskio_bind(NULL);
    free(system);
    free(logger);
    free(base);
    free(disk);
    G("cal real FatFs two1KiB files / prepare-sync-seal-sync-close-readback / startup reload / "
      "capture guard / every sector IO fail with0-17-511B torn writes / old file byte-exact / "
      "uncertain commit reconciled / config rebind never revives stale profile / legacy ADC migration preserves latest old slot");
}

static uint64_t cal_clock(void *ctx)
{
    return *(uint64_t *)ctx;
}
static void cal_wire_action(st2_server *server, uint16_t id, st_cal_action action, uint64_t now)
{
    st2_packet p = {.type = ST2_CAL_COMMAND, .epoch = server->epoch, .id = id};
    p.data[0] = (uint8_t)action;
    if (action == ST_CAL_ACTION_REFERENCE)
    {
        p.data[1] = ST_CAL_GRAMS;
        st_put64(p.data + 2, 100000);
        st_put32(p.data + 10, ST_CAL_G0_UM_S2);
    }
    uint8_t in[ST2_MAX], out[ST2_MAX];
    size_t n = st2_encode(&p, in);
    C(n == 32);
    C(st2_server_receive(server, in, n, (st2_measurement){0}, now, out) == 35);
}
static void cal_dma_window(st_cal_system *system, uint64_t *now, int32_t raw, uint32_t seq)
{
    *now += system->service.policy.settle_us;
    for (unsigned i = 0; i < 16; i++)
    {
        st_capture c = {.time_us = *now, .sequence = seq + i};
        c.bytes[0] = (uint8_t)((uint32_t)raw >> 16);
        c.bytes[1] = (uint8_t)((uint32_t)raw >> 8);
        c.bytes[2] = (uint8_t)raw;
        C(st_cal_input_offer_isr(&system->endpoint.input, &c));
        *now += 500;
    }
    st_cal_endpoint_step(&system->endpoint);
}
static void cal_wire_candidate(st_cal_system *s, st2_server *server, uint16_t *id, uint64_t *now)
{
    cal_wire_action(server, (*id)++, ST_CAL_ACTION_BEGIN, *now);
    cal_wire_action(server, (*id)++, ST_CAL_ACTION_ZERO, *now);
    cal_dma_window(s, now, 1000, 100);
    C(s->service.workflow.state == ST_CAL_LOAD_READY);
    cal_wire_action(server, (*id)++, ST_CAL_ACTION_REFERENCE, *now);
    cal_wire_action(server, (*id)++, ST_CAL_ACTION_LOAD, *now);
    cal_dma_window(s, now, 2000, 200);
    C(s->service.workflow.state == ST_CAL_REVIEW);
}
static void cal_end_to_end(void)
{
    uint8_t *disk = calloc(8192, 512);
    C(disk != NULL);
    make_fat16(disk);
    block_bus bus = {.disk = disk, .sectors = 8192, .present = true, .ready = true};
    st_block_device dev;
    bind_block(&dev, &bus);
    dev.timeout_ms = 1000;
    st_diskio_bind(&dev);
    FATFS fs;
    C(f_mount(&fs, "", 1) == FR_OK);
    st_fatfs_worker worker;
    st_fatfs_init(&worker, true);
    st_logger *logger = malloc(sizeof(*logger));
    st_cal_system *s = malloc(sizeof(*s));
    C(logger && s);
    st_cal_profile model = st_cal_test_profile();
    st_cal_binding b = model.binding;
    uint64_t now = 1000000;
    st_log_config cfg = {.adc_clock_hz = b.adc_clock_hz,
                         .osr = b.osr,
                         .rate_millihz = b.rate_millihz,
                         .gain_ch0 = b.gain0,
                         .gain_ch1 = b.gain1,
                         .clock_verified = true,
                         .simulated = true};
    Log_ApplyAds1220Metadata(&cfg, false, 0);
    st_logger_init(logger, cfg, st_fatfs_port(&worker));
    st2_server server;
    st2_server_init(&server, logger, (st2_nv_port){0}, (st2_timing){10000, 2200, 2500, 100000, 4},
                    (uint8_t[12]){1}, (uint8_t[12]){2}, 0);
    server.active = true;
    server.offered = true;
    server.epoch = 33;
    C(st_cal_system_init(s, logger, &worker, &server, b, model.policy, &now, cal_clock) ==
      ST_CAL_DISK_EMPTY);
    uint16_t id = 1;
    cal_wire_candidate(s, &server, &id, &now);
    C(!s->active.valid && !logger->config.calibration_valid);
    uint16_t save_id = id++;
    cal_wire_action(&server, save_id, ST_CAL_ACTION_SAVE, now);
    C(s->active.valid && s->active.revision == 1 && s->service.workflow.state == ST_CAL_COMMITTED);
    unsigned before = bus.operations;
    cal_wire_action(&server, save_id, ST_CAL_ACTION_SAVE, now);
    C(bus.operations == before); /* duplicate SAVE never invokes FatFs again */
    C(server.calibration.prepare_start(server.calibration.ctx));
    C(logger->config.calibration_valid && logger->config.profile_version == 1);
    cal_wire_action(&server, id++, ST_CAL_ACTION_BEGIN, now);
    before = bus.operations;
    cal_wire_action(&server, id++, ST_CAL_ACTION_CANCEL, now);
    C(bus.operations == before && s->active.revision == 1 &&
      s->service.workflow.state == ST_CAL_CANCELLED);
    cal_wire_candidate(s, &server, &id, &now);
    bus.fail_at = bus.operations + 1;
    cal_wire_action(&server, id++, ST_CAL_ACTION_SAVE, now);
    C(s->active.revision == 1 && s->service.workflow.state == ST_CAL_FAILED &&
      s->service.workflow.error == ST_CAL_STORAGE_ERROR);
    bus.fail_at = 0;
    C(f_mount(NULL, "", 0) == FR_OK);
    bind_block(&dev, &bus);
    dev.timeout_ms = 1000;
    st_diskio_bind(&dev);
    C(f_mount(&fs, "", 1) == FR_OK);
    st_cal_profile reloaded = {0};
    st_cal_fatfs_init(&s->disk, &worker, logger, b);
    C(st_cal_fatfs_open(&s->disk, &reloaded) == ST_CAL_DISK_VALID && reloaded.revision == 1);
    C(f_mount(NULL, "", 0) == FR_OK);
    st_diskio_bind(NULL);
    free(s);
    free(logger);
    free(disk);
    G("cal end-to-end compact RF command -> readonly DMA queue -> service -> real FatFs/diskio -> "
      "verified active -> START metadata / duplicate SAVE no IO / cancel and injected write "
      "failure retain old / remount reload (synthetic card only)");
}

unsigned st_test_storage(unsigned *out)
{
    block_tests();
    diskio_tests();
    fatfs_logger_test();
    cooperative_storage_test();
    cal_fatfs_test();
    cal_end_to_end();
    nv_tests();
    nv_powercuts();
    *out += groups;
    return checks;
}
