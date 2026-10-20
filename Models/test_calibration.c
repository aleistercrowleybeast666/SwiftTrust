#include "st_cal.h"
#include "st_cal_service.h"
#include "st_ui.h"
#include <limits.h>
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
            fprintf(stderr, "calibration failed line%d: %s\n", __LINE__, #x);                      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define G(s)                                                                                       \
    do                                                                                             \
    {                                                                                              \
        groups++;                                                                                  \
        puts("PASS " s);                                                                           \
    } while (0)
static st_cal_binding binding(void)
{
    return (st_cal_binding){.profile_id = 7,
                            .sensor_id = 17,
                            .config_id = 27,
                            .adc_clock_hz = 8192000,
                            .osr = 2048,
                            .rate_millihz = 2000000,
                            .excitation_nominal_uv = 2500000,
                            .filter_id = 3,
                            .rated_force_nn = UINT64_C(500000000000),
                            .gain0 = 128,
                            .gain1 = 1,
                            .wire_count = 6,
                            .power_mode = 2,
                            .verified = true,
                            .simulated = true};
}
static st_cal_policy policy(void)
{
    return (st_cal_policy){16, 4, 100, 2000, 10000000, 60000000};
}
static st_capture capture(int32_t raw, uint32_t sequence, uint64_t time)
{
    st_capture c = {.time_us = time, .sequence = sequence};
    c.bytes[0] = 1;
    c.bytes[1] = 3;
    uint32_t value = (uint32_t)raw;
    c.bytes[3] = (uint8_t)(value >> 16);
    c.bytes[4] = (uint8_t)(value >> 8);
    c.bytes[5] = (uint8_t)value;
    uint16_t crc = st_crc16(c.bytes, 9);
    c.bytes[9] = (uint8_t)(crc >> 8);
    c.bytes[10] = (uint8_t)crc;
    return c;
}
static void collect(st_cal_workflow *w, st_cal_binding b, int32_t raw, uint32_t first)
{
    uint64_t start = w->collect_us + w->candidate.policy.settle_us;
    for (unsigned i = 0; i < 16; i++)
    {
        st_capture c = capture(raw + (i & 1u ? 1 : -1), first + i, start + (uint64_t)i * 500);
        st_capture old = c;
        C(st_cal_feed(w, b, &c));
        C(!memcmp(&old, &c, sizeof(c)));
    }
}
static void prepare(st_cal_workflow *w, const st_cal_profile *active, int32_t loaded)
{
    st_cal_binding b = binding();
    C(st_cal_begin(w, b, policy(), active, true, 100000));
    C(st_cal_start_zero(w, true, 101000));
    collect(w, b, 1000, 1);
    C(w->state == ST_CAL_LOAD_READY && w->candidate.zero_raw == 1000);
    C(st_cal_set_reference(w, (st_cal_reference){ST_CAL_KILOGRAMS, 1000, ST_CAL_G0_UM_S2}));
    C(st_cal_start_load(w, true, w->candidate.zero.last_us + 100000));
    collect(w, b, loaded, 100);
    C(w->state == ST_CAL_REVIEW && w->candidate.valid);
}
typedef struct
{
    uint8_t bytes[256];
    unsigned calls;
    bool fail;
    st_cal_profile *active;
    uint32_t expected_revision;
    uint64_t now;
    bool idle;
} context;
static bool save(void *ctx, const uint8_t bytes[256])
{
    context *c = ctx;
    c->calls++;
    C(c->active->revision == c->expected_revision);
    if (c->fail)
        return false;
    st_cal_profile parsed;
    C(st_cal_profile_decode(&parsed, bytes, 256));
    memcpy(c->bytes, bytes, 256);
    return true;
}
static uint64_t now_us(void *ctx)
{
    return ((context *)ctx)->now;
}
static bool is_idle(void *ctx)
{
    return ((context *)ctx)->idle;
}
static void units(void)
{
    int64_t force = 0;
    C(st_cal_reference_force((st_cal_reference){ST_CAL_KILOGRAMS, 1000, ST_CAL_G0_UM_S2}, &force) &&
      force == INT64_C(9806650000));
    C(st_cal_reference_force((st_cal_reference){ST_CAL_GRAMS, 100000, ST_CAL_G0_UM_S2}, &force) &&
      force == 980665000);
    C(st_cal_reference_force((st_cal_reference){ST_CAL_GRAMS, 1, ST_CAL_G0_UM_S2}, &force) &&
      force == 9807);
    C(st_cal_reference_force((st_cal_reference){ST_CAL_NEWTONS, 1250, 0}, &force) &&
      force == 1250000000);
    C(!st_cal_reference_force((st_cal_reference){ST_CAL_NEWTONS, 0, 0}, &force));
    C(!st_cal_reference_force((st_cal_reference){ST_CAL_NEWTONS, 1, ST_CAL_G0_UM_S2}, &force));
    C(!st_cal_reference_force((st_cal_reference){ST_CAL_GRAMS, 1, 0}, &force));
    C(!st_cal_reference_force((st_cal_reference){(st_cal_unit)-1, 1, ST_CAL_G0_UM_S2}, &force));
    C(!st_cal_reference_force((st_cal_reference){ST_CAL_KILOGRAMS, UINT64_MAX, ST_CAL_G0_UM_S2},
                              &force));
    G("cal explicit g/kg/N milli-units / saved standard gravity / nearest-nN conversion / zero and "
      "overflow / invalid unit and gravity");
}
static st_cal_profile good_profile(void)
{
    st_cal_profile active = {0};
    st_cal_workflow w;
    prepare(&w, &active, 2000);
    context ctx = {.active = &active};
    C(st_cal_commit(&w, &active, binding(), true, (st_cal_storage){&ctx, save}));
    C(active.revision == 1 && active.zero_raw == 1000 && active.nano_newtons_per_count == 9806650);
    return active;
}
static void profile_and_raw(void)
{
    st_cal_profile active = good_profile();
    uint8_t bytes[256], again[256];
    C(st_cal_profile_encode(&active, bytes) == 256);
    st_cal_profile parsed;
    C(st_cal_profile_decode(&parsed, bytes, 256));
    C(st_cal_profile_encode(&parsed, again) == 256 && !memcmp(bytes, again, 256));
    for (unsigned i = 0; i < 256; i++)
    {
        uint8_t bad[256];
        memcpy(bad, bytes, 256);
        bad[i] ^= 1;
        C(!st_cal_profile_decode(&parsed, bad, 256));
    }
    C(!st_cal_profile_decode(&parsed, bytes, 255));
    st_logger logger;
    st_log_config config = {.adc_clock_hz = 8192000,
                            .osr = 2048,
                            .rate_millihz = 2000000,
                            .boot_epoch = 1,
                            .gain_ch0 = 128,
                            .gain_ch1 = 1,
                            .clock_verified = true,
                            .simulated = true};
    st_logger_init(&logger, config, (st_file_port){0});
    st_capture c = capture(2000, 1, 1000000), old = c;
    uint8_t raw_before[32], raw_after[32];
    st_record(raw_before, &c, 0, 0);
    C(st_cal_apply_logger(&active, binding(), &logger));
    st_record(raw_after, &c, 0, 0);
    C(!memcmp(raw_before, raw_after, 32) && !memcmp(&old, &c, sizeof(c)));
    C(logger.config.calibration_valid && logger.config.zero_raw == 1000 &&
      logger.config.nano_newtons_per_count == 9806650);
    st_cal_binding changed = binding();
    changed.excitation_nominal_uv++;
    C(!st_cal_apply_logger(&active, changed, &logger) && !logger.config.calibration_valid);
    C(active.nano_newtons_per_count == 9806650);
    logger.state = ST_CAPTURING;
    C(!st_cal_apply_logger(&active, binding(), &logger));
    logger.state = ST_IDLE;
    logger.config.simulated = false;
    C(!st_cal_apply_logger(&active, binding(), &logger));
    st_cal_workflow negative;
    prepare(&negative, &active, 0);
    C(negative.candidate.nano_newtons_per_count == -9806650);
    G("cal profile CRC roundtrip and every-byte corruption / negative polarity / binding "
      "invalidation / simulation provenance / raw capture and record bytes unchanged");
}
static void failure_rollback(void)
{
    st_cal_profile active = good_profile(), old = active;
    st_cal_workflow w;
    C(st_cal_begin(&w, binding(), policy(), &active, true, 100000));
    C(st_cal_start_zero(&w, true, 101000));
    st_capture c = capture(1000, 1, 103000);
    c.bytes[9] ^= 1;
    C(!st_cal_feed(&w, binding(), &c) && w.error == ST_CAL_ADC &&
      !memcmp(&active, &old, sizeof(old)));
    C(st_cal_begin(&w, binding(), policy(), &active, true, 100000));
    C(st_cal_start_zero(&w, true, 101000));
    c = capture(1000, 1, 103000);
    C(st_cal_feed(&w, binding(), &c));
    c = capture(1010, 2, 103500);
    C(!st_cal_feed(&w, binding(), &c) && w.error == ST_CAL_UNSTABLE);
    C(st_cal_begin(&w, binding(), policy(), &active, true, 100000));
    C(st_cal_start_zero(&w, true, 101000));
    c = capture(1000, 1, 103000);
    C(st_cal_feed(&w, binding(), &c));
    c = capture(1000, 3, 103500);
    C(!st_cal_feed(&w, binding(), &c) && w.error == ST_CAL_SEQUENCE);
    C(st_cal_begin(&w, binding(), policy(), &active, true, 100000));
    C(st_cal_start_zero(&w, true, 101000));
    st_cal_tick(&w, UINT64_C(101000) + policy().timeout_us + 1);
    C(w.error == ST_CAL_TIMEOUT);
    C(st_cal_begin(&w, binding(), policy(), &active, true, 100000));
    C(st_cal_start_zero(&w, true, 101000));
    collect(&w, binding(), 1000, 1);
    C(!st_cal_set_reference(&w, (st_cal_reference){ST_CAL_KILOGRAMS, 1000000, ST_CAL_G0_UM_S2}));
    C(w.state == ST_CAL_LOAD_READY && w.error == ST_CAL_REFERENCE);
    C(st_cal_set_reference(&w, (st_cal_reference){ST_CAL_NEWTONS, 1000, 0}));
    C(!st_cal_start_load(&w, true, w.candidate.zero.last_us + policy().zero_max_age_us + 1) &&
      w.error == ST_CAL_ZERO_EXPIRED);
    prepare(&w, &active, 2000);
    context ctx = {.active = &active, .expected_revision = 1, .fail = true};
    C(!st_cal_commit(&w, &active, binding(), true, (st_cal_storage){&ctx, save}) &&
      w.error == ST_CAL_STORAGE_ERROR);
    C(!memcmp(&active, &old, sizeof(old)) && ctx.calls == 1);
    prepare(&w, &active, 2000);
    C(!st_cal_commit(&w, &active, binding(), true, (st_cal_storage){0}) &&
      w.error == ST_CAL_STORAGE_UNBOUND && !memcmp(&active, &old, sizeof(old)));
    prepare(&w, &active, 2000);
    st_cal_cancel(&w);
    C(w.state == ST_CAL_CANCELLED && !memcmp(&active, &old, sizeof(old)));
    prepare(&w, &active, 2000);
    st_cal_binding changed = binding();
    changed.gain0 = 64;
    C(!st_cal_commit(&w, &active, changed, true, (st_cal_storage){&ctx, save}) &&
      w.error == ST_CAL_CONFIG && !memcmp(&active, &old, sizeof(old)));
    prepare(&w, &active, 2000);
    active.revision = 2;
    C(!st_cal_commit(&w, &active, binding(), true, (st_cal_storage){&ctx, save}) &&
      w.error == ST_CAL_CONFLICT && active.revision == 2);
    active = old;
    C(!st_cal_begin(&w, binding(), policy(), &active, false, 100000) && w.error == ST_CAL_BUSY);
    G("cal CRC/noise/gap/timeout/expired-zero rejection / reference range / save failure or "
      "unbound storage / cancellation / changed config and concurrent profile preserve old active "
      "values");
}
static void rotate(st_ui *u, st2_client *c, int count)
{
    for (int i = 0; i < (count < 0 ? -count : count); i++)
        C(!st_ui2_handle(u, count < 0 ? ST_UI_PREV : ST_UI_NEXT, c, 10));
}
static void press(st_ui *u, st2_client *c)
{
    C(!st_ui2_handle(u, ST_UI_PRESS, c, 10));
    C(!c->pending);
}
static void feed_service(st_cal_service *s, context *ctx, int32_t raw, uint32_t first)
{
    uint64_t start = s->workflow.collect_us + s->policy.settle_us;
    for (unsigned i = 0; i < 16; i++)
    {
        ctx->now = start + (uint64_t)i * 500;
        st_capture c = capture(raw + (i & 1u ? 1 : -1), first + i, ctx->now);
        C(st_cal_service_feed(s, &c));
    }
}
static void frame(FILE *out, st_ui *u, st2_client *client)
{
    st_ui_model model;
    st_ui2_render(u, client, (st_voltage_view){.kind = ST_V_MEASURED, .mv = 4100}, 10, &model);
    uint8_t pixels[1024];
    st_ui_raster(&model, pixels);
    C(fwrite(pixels, 1, 1024, out) == 1024);
}
static void set_digit(st_ui *u, st2_client *c, st_cal_ui *cal, unsigned digit, unsigned value)
{
    C(cal->page == ST_CAL_UI_EDITOR && !cal->editing);
    rotate(u, c, (int)digit - (int)cal->selection);
    press(u, c);
    C(cal->editing);
    for (unsigned i = 0; i < value; i++)
        rotate(u, c, 1);
    press(u, c);
    C(!cal->editing);
}
static void ui_workflow(const char *images)
{
    st_cal_profile active = good_profile(), old = active;
    st_cal_binding b = binding();
    context ctx = {.active = &active, .expected_revision = 1, .now = 1000000, .idle = true};
    st_cal_service service;
    st_cal_service_init(&service, &active, &b, policy(), (st_cal_storage){&ctx, save}, &ctx, now_us,
                        is_idle);
    st_cal_ui cal;
    st_cal_ui_init(&cal, st_cal_service_port(&service));
    st_ui ui;
    st_ui_init(&ui);
    st_ui_attach_calibration(&ui, &cal);
    st2_client client = {.link = ST2_LINK_UP,
                         .have_status = true,
                         .received_ms = 0,
                         .flags = 64,
                         .timing = {10000, 2200, 2500, 100000, 4}};
    rotate(&ui, &client, 2);
    press(&ui, &client);
    C(ui.page == ST_UI_CAPTURE);
    rotate(&ui, &client, 2);
    press(&ui, &client);
    C(ui.page == ST_UI_CALIBRATION && cal.page == ST_CAL_UI_INTRO && cal.selection == 1);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_ZERO && cal.selection == 1);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_WAIT_ZERO);
    feed_service(&service, &ctx, 1000, 1);
    st_cal_panel panel;
    st_cal_ui_render(&cal, &panel);
    C(cal.page == ST_CAL_UI_LOAD_KIND && cal.selection == 2);
    rotate(&ui, &client, 1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_PRESETS && cal.selection == 7);
    rotate(&ui, &client, 4);
    C(cal.selection == 3);
    FILE *out = fopen(images, "wb");
    C(out != NULL);
    frame(out, &ui, &client);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_REFERENCE && cal.draft.unit == ST_CAL_KILOGRAMS &&
      cal.draft.milli_units == 1000 && cal.selection == 1);
    C(!service.workflow.have_reference);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_PRESETS && cal.selection == 7);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_LOAD_KIND);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_UNITS && cal.selection == 3);
    rotate(&ui, &client, -2);
    C(cal.selection == 1);
    frame(out, &ui, &client);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_EDITOR && cal.edit_value == 0 && !cal.editing);
    set_digit(&ui, &client, &cal, 5, 1);
    set_digit(&ui, &client, &cal, 6, 2);
    set_digit(&ui, &client, &cal, 7, 3);
    set_digit(&ui, &client, &cal, 8, 4);
    C(cal.edit_value == 1234 && !cal.unit_reset);
    frame(out, &ui, &client);
    press(&ui, &client);
    C(cal.editing);
    frame(out, &ui, &client);
    press(&ui, &client);
    st_ui_model inverse, normal;
    uint8_t a[1024], n[1024];
    st_ui2_render(&ui, &client, (st_voltage_view){0}, 10, &inverse);
    C(inverse.inverse && inverse.inverse_row == 3 && inverse.inverse_column == 9);
    normal = inverse;
    normal.inverse = false;
    st_ui_raster(&inverse, a);
    st_ui_raster(&normal, n);
    for (unsigned i = 0; i < 1024; i++)
    {
        unsigned begin = 3u * 128u + 9u * 6u;
        C(a[i] == (uint8_t)(n[i] ^ ((i >= begin && i < begin + 6u) ? 255u : 0u)));
    }
    rotate(&ui, &client, 1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_REFERENCE && cal.selection == 1);
    st_cal_ui_render(&cal, &panel);
    C(strstr(panel.rows[1], "1.234 kg") && strstr(panel.rows[2], "9.80665"));
    frame(out, &ui, &client);
    {
        st_ui_model units_model;
        uint8_t pixels[1024];
        st_ui2_render(&ui, &client, (st_voltage_view){0}, 10, &units_model);
        st_ui_raster(&units_model, pixels);
        for (unsigned cell = 6; cell < 8; cell++)
        {
            unsigned bits = 0;
            for (unsigned i = 0; i < 5; i++)
                bits |= pixels[3u * 128u + cell * 6u + i];
            C(bits != 0);
        }
    }
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_LOAD && cal.selection == 1);
    C(service.workflow.candidate.reference_force_nn == INT64_C(12101406100));
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_WAIT_LOAD);
    ctx.now += 100000;
    feed_service(&service, &ctx, 2000, 100);
    st_cal_ui_render(&cal, &panel);
    C(cal.page == ST_CAL_UI_REVIEW && cal.selection == 1);
    C(!memcmp(&active, &old, sizeof(old)) && ctx.calls == 0);
    frame(out, &ui, &client);
    C(fclose(out) == 0);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_RESULT && active.revision == 2 && ctx.calls == 1 &&
      cal.view.state == ST_CAL_COMMITTED);
    press(&ui, &client);
    C(ui.page == ST_UI_CAPTURE && ui.selection == 2 && !client.pending);
    G("cal encoder-only preset/custom workflow / safe-default confirmations / full value and unit "
      "/ select-versus-edit / monochrome inverse digit / no RF transaction / save only after "
      "reviewed candidate");
}
static void editor_boundaries(void)
{
    st_cal_profile active = good_profile(), old = active;
    st_cal_binding b = binding();
    context ctx = {.active = &active, .expected_revision = 1, .now = 1000000, .idle = true};
    st_cal_service service;
    st_cal_service_init(&service, &active, &b, policy(), (st_cal_storage){&ctx, save}, &ctx, now_us,
                        is_idle);
    st_cal_ui cal;
    st_cal_ui_init(&cal, st_cal_service_port(&service));
    st_ui ui;
    st_ui_init(&ui);
    st_ui_attach_calibration(&ui, &cal);
    ui.page = ST_UI_CALIBRATION;
    st2_client client = {.link = ST2_LINK_UP,
                         .have_status = true,
                         .flags = 0,
                         .timing = {10000, 2200, 2500, 100000, 4}};
    C(service.workflow.state == ST_CAL_INACTIVE);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    feed_service(&service, &ctx, 1000, 1);
    st_cal_panel panel;
    st_cal_ui_render(&cal, &panel);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    rotate(&ui, &client, -3);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_EDITOR && cal.draft.unit == ST_CAL_GRAMS);
    rotate(&ui, &client, 9);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_EDITOR && cal.local_error == ST_CAL_REFERENCE && ctx.calls == 0);
    rotate(&ui, &client, -9);
    press(&ui, &client);
    rotate(&ui, &client, -1);
    C(cal.edit_value == 900000000);
    rotate(&ui, &client, 1);
    C(cal.edit_value == 0);
    press(&ui, &client);
    for (unsigned digit = 0; digit < 9; digit++)
        set_digit(&ui, &client, &cal, digit, 9);
    C(cal.edit_value == 999999999);
    rotate(&ui, &client, 1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_REFERENCE);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_REFERENCE && cal.view.error == ST_CAL_REFERENCE);
    rotate(&ui, &client, 1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_EDITOR && cal.selection == 9);
    rotate(&ui, &client, 1);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_UNITS);
    rotate(&ui, &client, -2);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_EDITOR && cal.draft.unit == ST_CAL_KILOGRAMS && cal.edit_value == 0 &&
      cal.unit_reset);
    st_cal_ui_render(&cal, &panel);
    C(strstr(panel.rows[5], "VALUE CLEAR"));
    rotate(&ui, &client, 10);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_UNITS);
    press(&ui, &client);
    C(cal.page == ST_CAL_UI_LOAD_KIND);
    press(&ui, &client);
    C(ui.page == ST_UI_CAPTURE);
    C(!memcmp(&active, &old, sizeof(old)) && ctx.calls == 0);
    ctx.idle = false;
    ui.page = ST_UI_CALIBRATION;
    st_cal_ui_open(&cal);
    rotate(&ui, &client, -1);
    press(&ui, &client);
    C(cal.view.error == ST_CAL_BUSY || cal.local_error == ST_CAL_BUSY);
    C(!memcmp(&active, &old, sizeof(old)));
    G("cal editor0..9 wrap and fixed decimal / nine-digit upper bound / zero and over-range "
      "refusal / unit switch explicitly clears draft / visible return paths / busy gate and cancel "
      "retain old profile");
}
unsigned st_test_calibration(unsigned *out, const char *images)
{
    units();
    profile_and_raw();
    failure_rollback();
    ui_workflow(images);
    editor_boundaries();
    *out += groups;
    return checks;
}
