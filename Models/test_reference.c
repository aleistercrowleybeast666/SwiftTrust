#include "st_reference.h"
#include "st_pairing.h"
#include "st_journal.h"
#include "st_console.h"
#include "st_auto_link.h"
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
    CHECK(!st_ref_rate_ok(960, 500000));
    CHECK(!st_ref_rate_ok(1030, 500000));
    CHECK(!st_ref_rate_ok(1, 100));
    const char *valid =
        "ACK_SENSOR_RATING=1\nPROFILE_ID=1\nSENSOR_ID=2\nWIRE_COUNT=6\nRATED_FORCE_MN="
        "1000000\nEXCITATION_UV=5000000\nMAX_SPAN_COUNTS=100\nMIN_DELTA_COUNTS=1000\n"
        "PGA_GAIN=128\nPGA_BYPASS=0\nSENSITIVITY_NV_V=2000000\n"
        "COMMON_MODE_MIN_UV=2400000\nCOMMON_MODE_MAX_UV=2600000\n";
    st_ref_sensor sensor;
    CHECK(st_ref_sensor_parse(&sensor, valid, strlen(valid)));
    CHECK(sensor.configured && sensor.binding.gain0 == 128 && sensor.binding.rate_millihz == ST_ADC_RATE_MILLIHZ);
    CHECK(sensor.binding.adc_model == 1220 && sensor.binding.reference_uv == 5000000 &&
          sensor.adc_config.max_differential_uv == 10000 && !sensor.adc_config.pga_bypass);
    CHECK(sensor.binding.rated_force_nn == UINT64_C(1000000000000));
    uint32_t electrical_id = sensor.binding.config_id;
    char bad[512];
    (void)snprintf(bad,sizeof bad,"%sAUTO_ZERO=1\n",valid);
    CHECK(st_ref_sensor_parse(&sensor,bad,strlen(bad))&&sensor.auto_zero);
    CHECK(sensor.binding.config_id == electrical_id);
    (void)snprintf(bad,sizeof bad,"%sAUTO_ZERO=2\n",valid);
    CHECK(!st_ref_sensor_parse(&sensor,bad,strlen(bad)));
    (void)snprintf(bad, sizeof bad, "%sACK_SENSOR_RATING=1\n", valid);
    CHECK(!st_ref_sensor_parse(&sensor, bad, strlen(bad)));
    CHECK(!sensor.configured);
    strcpy(bad, valid);
    char *rating = strstr(bad, "RATED_FORCE_MN=1000000");
    CHECK(rating);
    rating[15] = '-';
    CHECK(!st_ref_sensor_parse(&sensor, bad, strlen(bad)));
    CHECK(!st_ref_sensor_parse(&sensor, "# no rating\n", 12));
    strcpy(bad, valid);
    char *sensitivity = strstr(bad, "SENSITIVITY_NV_V=2000000");
    CHECK(sensitivity);
    sensitivity[16] = '9'; /* 9 mV/V at PGA128 exceeds the usable input range. */
    CHECK(!st_ref_sensor_parse(&sensor, bad, strlen(bad)));
    strcpy(bad, valid);
    char *common_mode = strstr(bad, "COMMON_MODE_MIN_UV=2400000");
    CHECK(common_mode);
    common_mode[19] = '0';
    CHECK(!st_ref_sensor_parse(&sensor, bad, strlen(bad)));
    const char *legacy = "ACK_SENSOR_RATING=1\nPROFILE_ID=1\nSENSOR_ID=2\nWIRE_COUNT=6\n"
                         "RATED_FORCE_MN=1000\nEXCITATION_UV=2500000\n"
                         "MAX_SPAN_COUNTS=100\nMIN_DELTA_COUNTS=1000\n";
    CHECK(!st_ref_sensor_parse(&sensor, legacy, strlen(legacy)) && !sensor.configured);
    CHECK(st2_timing_valid(st_ref_link_timing()));
    st_ads_config adc = st_ref_ads_config(true);
    CHECK(adc.clock_hz == 4096000 && adc.reference_uv == 5000000 && adc.operating_mode == 2 &&
          adc.pga_bypass && adc.gain == 1 && !adc.input_range_known && st_ads_config_valid(adc));
    st_radio_config radio = st_ref_radio_config(true);
    CHECK(radio.frequency_hz == 2473000000u && radio.tx_timeout_ms == 500);
}
static void automatic_link(void)
{
    media acquisition_media, controller_media;
    fresh(&acquisition_media); fresh(&controller_media);
    st_journal acquisition_journal, controller_journal;
    st_pairing acquisition_pair, controller_pair;
    CHECK(st_journal_open(&acquisition_journal, port(&acquisition_media)));
    CHECK(st_journal_open(&controller_journal, port(&controller_media)));
    CHECK(st_pairing_open(&acquisition_pair, st_journal_storage(&acquisition_journal), own,
                          ST_NV_ACQUISITION) == ST_NV_EMPTY);
    CHECK(st_pairing_open(&controller_pair, st_journal_storage(&controller_journal), peer,
                          ST_NV_CONTROLLER) == ST_NV_EMPTY);
    AutoLink acquisition, controller;
    AutoLink_Init(&acquisition, &acquisition_pair, st_ref_ignition_key(), ST_PROJECT_PAIR_ID,
                   &acquisition_media, idle);
    AutoLink_Init(&controller, &controller_pair, st_ref_ignition_key(), ST_PROJECT_PAIR_ID,
                   &controller_media, idle);
    uint8_t request[ST2_MAX], reply[ST2_MAX], bad[ST2_MAX], ignored[ST2_MAX]; size_t length = 0;
    CHECK(AutoLink_TxDue(&controller, 0, request, &length) && length == AUTO_LINK_BYTES);
    AutoLink another_set=acquisition;another_set.network=ST_PROJECT_PAIR_ID+1u;
    CHECK(!AutoLink_Receive(&another_set,request,length,reply));
    CHECK(acquisition_pair.store.status==ST_NV_EMPTY&&!acquisition_media.calls);
    CHECK(!AutoLink_TxDue(&controller, 1999, bad, &length));
    for (unsigned i = 0; i < AUTO_LINK_BYTES; i++)
    {
        memcpy(bad, request, AUTO_LINK_BYTES); bad[i] ^= 1;
        CHECK(!AutoLink_Receive(&acquisition, bad, AUTO_LINK_BYTES, reply));
    }
    memcpy(bad, request, AUTO_LINK_BYTES); bad[20] ^= 1;
    st_put16(bad+AUTO_LINK_BYTES-2, st_crc16(bad,AUTO_LINK_BYTES-2));
    CHECK(!AutoLink_Receive(&acquisition, bad, AUTO_LINK_BYTES, reply));
    CHECK(acquisition_pair.store.status == ST_NV_EMPTY && !acquisition_media.calls);
    acquisition_media.idle = false;
    CHECK(!AutoLink_Receive(&acquisition, request, AUTO_LINK_BYTES, reply));
    acquisition_media.idle = true;
    CHECK(AutoLink_Receive(&acquisition, request, AUTO_LINK_BYTES, reply) == AUTO_LINK_BYTES);
    CHECK(AutoLink_TakeChanged(&acquisition) && !AutoLink_TakeChanged(&acquisition));
    CHECK(acquisition_pair.store.status == ST_NV_VALID);
    memcpy(bad, reply, AUTO_LINK_BYTES); bad[16] ^= 1; st_put16(bad+AUTO_LINK_BYTES-2,st_crc16(bad,AUTO_LINK_BYTES-2));
    CHECK(!AutoLink_Receive(&controller, bad, AUTO_LINK_BYTES, ignored));
    CHECK(controller_pair.store.status == ST_NV_EMPTY);
    CHECK(!AutoLink_Receive(&controller, reply, AUTO_LINK_BYTES, ignored));
    CHECK(AutoLink_TakeChanged(&controller) && controller_pair.store.status == ST_NV_VALID);
    CHECK(!memcmp(acquisition_pair.store.identity.peer_uid, peer, 12));
    CHECK(!memcmp(controller_pair.store.identity.peer_uid, own, 12));
    unsigned writes = acquisition_media.calls;
    CHECK(AutoLink_Receive(&acquisition, request, AUTO_LINK_BYTES, reply) == AUTO_LINK_BYTES);
    CHECK(acquisition_media.calls == writes && !AutoLink_TakeChanged(&acquisition));
    CHECK(st_journal_open(&acquisition_journal, port(&acquisition_media)));
    CHECK(st_pairing_open(&acquisition_pair, st_journal_storage(&acquisition_journal), own,
                          ST_NV_ACQUISITION) == ST_NV_VALID);
    CHECK(st_journal_open(&controller_journal, port(&controller_media)));
    CHECK(st_pairing_open(&controller_pair, st_journal_storage(&controller_journal), peer,
                          ST_NV_CONTROLLER) == ST_NV_VALID);
    st2_nv_port acq_nv = st_nv_link_port(&acquisition_pair.store);
    st2_nv_port ctrl_nv = st_nv_link_port(&controller_pair.store);
    uint64_t counter = 0; uint32_t epoch = 0;
    CHECK(ctrl_nv.reserve_counter(ctrl_nv.ctx, &counter) && counter == 1);
    CHECK(acq_nv.reserve_epoch(acq_nv.ctx, counter, &epoch) && epoch == 0x01000001);
    CHECK(!acq_nv.reserve_epoch(acq_nv.ctx, counter, &epoch));
    media third_media; fresh(&third_media);
    st_journal third_journal; st_pairing third_pair;
    CHECK(st_journal_open(&third_journal, port(&third_media)));
    uint8_t third_uid[12]; memcpy(third_uid,peer,12); third_uid[0]++;
    CHECK(st_pairing_open(&third_pair,st_journal_storage(&third_journal),third_uid,
                          ST_NV_CONTROLLER) == ST_NV_EMPTY);
    AutoLink third;
    AutoLink_Init(&third,&third_pair,st_ref_ignition_key(),ST_PROJECT_PAIR_ID,&third_media,idle);
    CHECK(AutoLink_TxDue(&third,0,bad,&length));
    CHECK(!AutoLink_Receive(&acquisition,bad,length,reply)); /* Cannot replace remembered peer. */
    CHECK(!memcmp(acquisition_pair.store.identity.peer_uid,peer,12));
    acquisition_pair.store.status = ST_NV_DAMAGED;
    CHECK(!AutoLink_Receive(&acquisition,request,AUTO_LINK_BYTES,reply));
}
int main(void)
{
    journal_cycles();
    torn_writes();
    compaction_cuts();
    console_pair();
    reference_parameters();
    automatic_link();
    printf("PASS reference integration: 6 groups, %u checks; automatic UID link, 256 torn writes, 97 compaction cuts, "
           "128 reboot/counter cycles\n",
           checks);
    return 0;
}
