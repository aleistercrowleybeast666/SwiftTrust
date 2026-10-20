#include "st_reference.h"
#include "st_pairing.h"
#include "st_journal.h"
#include "st_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
typedef struct
{
    uint8_t bytes[2][1024];
    bool idle;
    int fail, partial;
    unsigned calls, erases;
    bool erase_fail;
    unsigned erase_partial;
} media;
static bool rd(void *ctx, unsigned r, uint32_t at, uint8_t *out, size_t n)
{
    media *m = ctx;
    memcpy(out, m->bytes[r] + at, n);
    return true;
}
static bool prog(void *ctx, unsigned r, uint32_t at, const uint8_t in[8])
{
    media *m = ctx;
    unsigned call = m->calls++;
    unsigned n = (int)call == m->fail ? (unsigned)m->partial : 8u;
    for (unsigned i = 0; i < n; i++)
    {
        CHECK((m->bytes[r][at + i] & in[i]) == in[i]);
        m->bytes[r][at + i] &= in[i];
    }
    return (int)call != m->fail;
}
static bool erase(void *ctx, unsigned r)
{
    media *m = ctx;
    memset(m->bytes[r], 255, m->erase_fail ? m->erase_partial : 1024u);
    m->erases++;
    return !m->erase_fail;
}
static bool idle(void *ctx)
{
    return ((media *)ctx)->idle;
}
static st_journal_port port(media *m)
{
    return (st_journal_port){m, rd, prog, erase, idle, 1024};
}
static void fresh(media *m)
{
    memset(m, 0, sizeof *m);
    memset(m->bytes, 255, sizeof m->bytes);
    m->idle = true;
    m->fail = -1;
}
static const uint8_t own[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t peer[12] = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
static void open_controller(st_journal *j, st_pairing *p, media *m)
{
    CHECK(st_journal_open(j, port(m)));
    st_nv_status status = st_pairing_open(p, st_journal_storage(j), own, ST_NV_CONTROLLER);
    CHECK(status == ST_NV_EMPTY || status == ST_NV_VALID);
}
static void journal_cycles(void)
{
    media m;
    fresh(&m);
    st_journal j;
    st_pairing p;
    open_controller(&j, &p, &m);
    CHECK(p.store.status == ST_NV_EMPTY);
    CHECK(st_pairing_provision(&p, peer, 0, true));
    for (uint64_t counter = 1; counter <= 128; counter++)
    {
        open_controller(&j, &p, &m);
        CHECK(p.store.status == ST_NV_VALID);
        CHECK(p.store.values.counter == counter - 1);
        st2_nv_port nv = st_nv_link_port(&p.store);
        uint64_t result = 0;
        CHECK(nv.reserve_counter(nv.ctx, &result) && result == counter);
    }
    CHECK(m.erases >= 30);
    open_controller(&j, &p, &m);
    CHECK(p.store.values.counter == 128);
    m.idle = false;
    uint64_t value = 0;
    unsigned writes = m.calls;
    st2_nv_port nv = st_nv_link_port(&p.store);
    CHECK(!nv.reserve_counter(nv.ctx, &value));
    CHECK(m.calls == writes);
    m.idle = true;
    open_controller(&j, &p, &m);
    CHECK(p.store.values.counter == 128);
    CHECK(!st_pairing_initialize_new(&p, true, ST_PAIR_NEW_MEDIA_CONFIRM));
    CHECK(!st_pairing_provision(&p, peer, 0, true));
}
static void torn_writes(void)
{
    media base;
    fresh(&base);
    st_journal j;
    st_pairing p;
    open_controller(&j, &p, &base);
    CHECK(st_pairing_provision(&p, peer, 0, true));
    for (int cut = 0; cut < 32; cut++)
        for (int partial = 0; partial < 8; partial++)
        {
            media m = base;
            m.calls = 0;
            m.fail = cut;
            m.partial = partial;
            open_controller(&j, &p, &m);
            st2_nv_port nv = st_nv_link_port(&p.store);
            uint64_t counter = 0;
            CHECK(!nv.reserve_counter(nv.ctx, &counter));
            CHECK(j.fault);
            CHECK(!counter);
            st_journal reboot;
            bool ok = st_journal_open(&reboot, port(&m));
            if (cut == 0 && partial == 0)
            {
                CHECK(ok);
                st_pairing other;
                CHECK(st_pairing_open(&other, st_journal_storage(&reboot), own, 1) == ST_NV_VALID);
                CHECK(!other.store.values.counter);
            }
            else
            {
                CHECK(!ok);
                CHECK(reboot.fault);
            }
        }
    /* Corrupt a committed latest image: old image must never be replayed. */
    media m = base;
    open_controller(&j, &p, &m);
    st2_nv_port nv = st_nv_link_port(&p.store);
    uint64_t counter;
    CHECK(nv.reserve_counter(nv.ctx, &counter));
    m.bytes[0][256 + 40] ^= 1;
    CHECK(!st_journal_open(&j, port(&m)));
    CHECK(j.fault);
}
static void compaction_cuts(void)
{
    media base;
    fresh(&base);
    st_journal j;
    st_pairing p;
    open_controller(&j, &p, &base);
    CHECK(st_pairing_provision(&p, peer, 0, true));
    for (unsigned i = 0; i < 7; i++)
    {
        uint64_t count;
        st2_nv_port nv = st_nv_link_port(&p.store);
        CHECK(nv.reserve_counter(nv.ctx, &count));
    }
    CHECK(j.next == 1024 && j.region == 1 && p.store.values.counter == 7);
    for (unsigned cut = 0; cut <= 1024; cut += 16)
    {
        media m = base;
        m.erase_fail = true;
        m.erase_partial = cut;
        open_controller(&j, &p, &m);
        uint64_t counter = 0;
        st2_nv_port nv = st_nv_link_port(&p.store);
        CHECK(!nv.reserve_counter(nv.ctx, &counter));
        CHECK(j.fault);
        bool ok = st_journal_open(&j, port(&m));
        if (cut == 0 || cut == 1024)
        {
            CHECK(ok);
            CHECK(st_pairing_open(&p, st_journal_storage(&j), own, 1) == ST_NV_VALID);
            CHECK(p.store.values.counter == 7);
        }
        else
        {
            CHECK(!ok);
            CHECK(j.fault);
        }
    }
    for (int cut = 0; cut < 32; cut++)
    {
        media m = base;
        m.fail = cut;
        m.partial = 3;
        m.calls = 0;
        open_controller(&j, &p, &m);
        uint64_t counter = 0;
        st2_nv_port nv = st_nv_link_port(&p.store);
        CHECK(!nv.reserve_counter(nv.ctx, &counter));
        CHECK(!st_journal_open(&j, port(&m)) && j.fault);
    }
}
static char output[512];
static unsigned changed_count;
static void emit(void *ctx, const char *s)
{
    (void)ctx;
    size_t used = strlen(output);
    if (strlen(s) < sizeof output - used)
        strcpy(output + used, s);
}
static void changed(void *ctx)
{
    (void)ctx;
    changed_count++;
}
static void feed(st_console *c, const char *s)
{
    while (*s)
        st_console_feed(c, (uint8_t)*s++);
}
static void console_pair(void)
{
    media m;
    fresh(&m);
    st_journal j;
    st_pairing p;
    open_controller(&j, &p, &m);
    st_console c = {.pair = &p, .ctx = &m, .idle = idle, .emit = emit, .changed = changed};
    output[0] = 0;
    feed(&c, "INFO\r\n");
    CHECK(strstr(output, "0102030405060708090A0B0C"));
    feed(&c, "PAIR 0C0B0A090807060504030201 0\n");
    CHECK(p.store.status == ST_NV_VALID);
    CHECK(changed_count == 1);
    unsigned before = m.calls;
    feed(&c, "START\nFIRE\nNVINIT NEW\nPAIR 000000000000000000000000 0\n");
    CHECK(m.calls == before);
    CHECK(changed_count == 1);
    char long_line[100];
    memset(long_line, 'X', 98);
    long_line[98] = '\n';
    long_line[99] = 0;
    feed(&c, long_line);
    CHECK(m.calls == before);
    feed(&c, "INFO\n");
    CHECK(!c.used && !c.overflow);
    st_pairing foreign;
    uint8_t wrong[12];
    memset(wrong, 7, 12);
    CHECK(st_pairing_open(&foreign, st_journal_storage(&j), wrong, 1) == ST_NV_FOREIGN);
    CHECK(!st_pairing_initialize_new(&foreign, true, ST_PAIR_NEW_MEDIA_CONFIRM));
}
static void reference_parameters(void)
{
    uint32_t mv = 0;
    CHECK(st_ref_vdda_mv(1500, 3300, 1500, &mv) && mv == 3300);
    CHECK(st_ref_vdda_mv(1500, 3000, 1500, &mv) && mv == 3000);
    CHECK(!st_ref_vdda_mv(0, 3300, 1500, &mv));
    CHECK(!st_ref_vdda_mv(1500, 3300, 0, &mv));
    CHECK(!st_ref_vdda_mv(1500, 3300, 4095, &mv));
    CHECK(!st_ref_vdda_mv(1500, 2500, 1500, &mv));
    CHECK(!st_ref_vdda_mv(1500, 3300, 100, &mv));
    CHECK(st_ref_rate_ok(1000, 500000));
    CHECK(!st_ref_rate_ok(970, 500000));
    CHECK(!st_ref_rate_ok(1030, 500000));
    CHECK(!st_ref_rate_ok(1, 100));
    const char *valid =
        "ACK_SENSOR_RATING=1\nPROFILE_ID=1\nSENSOR_ID=2\nWIRE_COUNT=6\nRATED_FORCE_MN="
        "1000000\nEXCITATION_UV=2500000\nMAX_SPAN_COUNTS=100\nMIN_DELTA_COUNTS=1000\n";
    st_ref_sensor sensor;
    CHECK(st_ref_sensor_parse(&sensor, valid, strlen(valid)));
    CHECK(sensor.configured && sensor.binding.gain0 == 1 && sensor.binding.rate_millihz == 2000000);
    CHECK(sensor.binding.rated_force_nn == UINT64_C(1000000000000));
    char bad[512];
    (void)snprintf(bad, sizeof bad, "%sACK_SENSOR_RATING=1\n", valid);
    CHECK(!st_ref_sensor_parse(&sensor, bad, strlen(bad)));
    CHECK(!sensor.configured);
    strcpy(bad, valid);
    char *rating = strstr(bad, "RATED_FORCE_MN=1000000");
    CHECK(rating);
    rating[15] = '-';
    CHECK(!st_ref_sensor_parse(&sensor, bad, strlen(bad)));
    CHECK(!st_ref_sensor_parse(&sensor, "# no rating\n", 12));
    CHECK(st2_timing_valid(st_ref_link_timing()));
    st_ads_config adc = st_ref_ads_config(true);
    CHECK(adc.clock_hz == 8192000 && adc.osr == 2048 && adc.power_mode == 2);
    st_radio_config radio = st_ref_radio_config(true);
    CHECK(radio.frequency_hz == 2473000000u && radio.tx_timeout_ms == 500);
}
int main(void)
{
    journal_cycles();
    torn_writes();
    compaction_cuts();
    console_pair();
    reference_parameters();
    printf("PASS reference integration: 5 groups, %u checks; 256 torn writes, 97 compaction cuts, "
           "128 reboot/counter cycles\n",
           checks);
    return 0;
}
