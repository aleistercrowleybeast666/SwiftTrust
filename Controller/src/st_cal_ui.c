#include "st_cal_ui.h"
#include <stdio.h>
#include <string.h>
static const st_cal_reference presets[] = {
    {ST_CAL_GRAMS, 100000, ST_CAL_G0_UM_S2},   {ST_CAL_GRAMS, 200000, ST_CAL_G0_UM_S2},
    {ST_CAL_GRAMS, 500000, ST_CAL_G0_UM_S2},   {ST_CAL_KILOGRAMS, 1000, ST_CAL_G0_UM_S2},
    {ST_CAL_KILOGRAMS, 2000, ST_CAL_G0_UM_S2}, {ST_CAL_KILOGRAMS, 5000, ST_CAL_G0_UM_S2},
    {ST_CAL_KILOGRAMS, 10000, ST_CAL_G0_UM_S2}};
enum
{
    PRESET_COUNT = sizeof(presets) / sizeof(presets[0]),
    DIGITS = 9,
    EDITOR_OK = 9,
    EDITOR_BACK = 10
};
static const uint32_t place_value[] = {100000000, 10000000, 1000000, 100000, 10000,
                                       1000,      100,      10,      1};
static void coefficient_text(char out[22], int64_t value)
{
    char reverse[20];
    unsigned count = 0, at = 0;
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    do
    {
        reverse[count++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude);
    if (value < 0)
        out[at++] = '-';
    while (count)
        out[at++] = reverse[--count];
    out[at] = 0;
}
static const char *unit_name(st_cal_unit unit)
{
    return unit == ST_CAL_GRAMS ? "g" : (unit == ST_CAL_KILOGRAMS ? "kg" : "N");
}
static const char *error_name(st_cal_error error)
{
    static const char *names[] = {"OK",
                                  "CONFIG REQUIRED",
                                  "IDLE REQUIRED",
                                  "INVALID REFERENCE",
                                  "ADC INVALID",
                                  "SAMPLE GAP/CLOCK",
                                  "CLOCK ERROR",
                                  "SAMPLE TIMEOUT",
                                  "LOAD UNSTABLE",
                                  "SPAN TOO SMALL",
                                  "PROFILE CONFLICT",
                                  "STORAGE UNBOUND",
                                  "SAVE FAILED",
                                  "ZERO EXPIRED",
                                  "COEFF TOO SMALL"};
    return (unsigned)error < sizeof(names) / sizeof(names[0]) ? names[error] : "ERROR";
}
static void value_text(char *out, size_t size, st_cal_reference ref, bool fixed)
{
    if (fixed)
        (void)snprintf(out, size, "%06lu.%03lu %s", (unsigned long)(ref.milli_units / 1000),
                       (unsigned long)(ref.milli_units % 1000), unit_name(ref.unit));
    else
        (void)snprintf(out, size, "%lu.%03lu %s", (unsigned long)(ref.milli_units / 1000),
                       (unsigned long)(ref.milli_units % 1000), unit_name(ref.unit));
}
static void page(st_cal_ui *u, st_cal_ui_page next, unsigned selected)
{
    u->page = next;
    u->selection = selected;
    u->editing = false;
    u->local_error = false;
}
static void refresh(st_cal_ui *u)
{
    if (!u->port.snapshot || !u->port.snapshot(u->port.ctx, &u->view))
    {
        u->view = (st_cal_view){.error = ST_CAL_CONFIG};
        return;
    }
    if (u->view.state == ST_CAL_FAILED && u->page != ST_CAL_UI_INTRO)
        page(u, ST_CAL_UI_RESULT, 0);
    else if (u->page == ST_CAL_UI_WAIT_ZERO && u->view.state == ST_CAL_LOAD_READY)
        page(u, ST_CAL_UI_LOAD_KIND, 2);
    else if (u->page == ST_CAL_UI_WAIT_LOAD && u->view.state == ST_CAL_REVIEW)
        page(u, ST_CAL_UI_REVIEW, 1);
}
void st_cal_ui_init(st_cal_ui *u, st_cal_control_port port)
{
    *u = (st_cal_ui){.port = port};
    st_cal_ui_open(u);
}
void st_cal_ui_open(st_cal_ui *u)
{
    page(u, ST_CAL_UI_INTRO, 1);
    u->draft = (st_cal_reference){0};
    u->edit_value = 0;
    u->unit_reset = false;
    refresh(u);
}
static bool send(st_cal_ui *u, st_cal_action action, const st_cal_reference *ref, bool idle)
{
    if (action != ST_CAL_ACTION_CANCEL && !idle)
    {
        u->view.error = ST_CAL_BUSY;
        u->local_error = ST_CAL_BUSY;
        return false;
    }
    if (!u->port.request || !u->port.request(u->port.ctx, action, ref))
    {
        refresh(u);
        u->local_error = u->view.error ? u->view.error : ST_CAL_CONFIG;
        return false;
    }
    refresh(u);
    return true;
}
static unsigned options(st_cal_ui_page p)
{
    switch (p)
    {
    case ST_CAL_UI_LOAD_KIND:
        return 3;
    case ST_CAL_UI_PRESETS:
        return PRESET_COUNT + 1;
    case ST_CAL_UI_UNITS:
        return 4;
    case ST_CAL_UI_EDITOR:
        return 11;
    case ST_CAL_UI_INTRO:
    case ST_CAL_UI_ZERO:
    case ST_CAL_UI_REFERENCE:
    case ST_CAL_UI_LOAD:
    case ST_CAL_UI_REVIEW:
        return 2;
    default:
        return 1;
    }
}
static bool cancel(st_cal_ui *u)
{
    (void)send(u, ST_CAL_ACTION_CANCEL, NULL, true);
    return true;
}
static bool back(st_cal_ui *u)
{
    switch (u->page)
    {
    case ST_CAL_UI_EDITOR:
        if (u->editing)
        {
            u->edit_value = u->edit_backup;
            u->editing = false;
            return false;
        }
        page(u, ST_CAL_UI_UNITS, 3);
        return false;
    case ST_CAL_UI_UNITS:
    case ST_CAL_UI_PRESETS:
        page(u, ST_CAL_UI_LOAD_KIND, 2);
        return false;
    case ST_CAL_UI_REFERENCE:
        page(u, u->reference_parent,
             u->reference_parent == ST_CAL_UI_EDITOR ? EDITOR_OK : PRESET_COUNT);
        return false;
    case ST_CAL_UI_LOAD:
        page(u, ST_CAL_UI_LOAD_KIND, 2);
        return false;
    default:
        return cancel(u);
    }
}
bool st_cal_ui_handle(st_cal_ui *u, st_cal_ui_event event, bool idle)
{
    refresh(u);
    if (event == ST_CAL_UI_BACK)
        return back(u);
    if (event == ST_CAL_UI_NEXT || event == ST_CAL_UI_PREV)
    {
        if (u->page == ST_CAL_UI_EDITOR && u->editing)
        {
            uint32_t place = place_value[u->selection], digit = (u->edit_value / place) % 10;
            uint32_t next = (digit + (event == ST_CAL_UI_NEXT ? 1u : 9u)) % 10u;
            u->edit_value = u->edit_value - digit * place + next * place;
        }
        else
        {
            unsigned count = options(u->page);
            u->selection = (u->selection + (event == ST_CAL_UI_NEXT ? 1 : count - 1)) % count;
        }
        u->local_error = false;
        return false;
    }
    if (event != ST_CAL_UI_PRESS)
        return false;
    switch (u->page)
    {
    case ST_CAL_UI_INTRO:
        if (u->selection == 1)
            return cancel(u);
        if (send(u, ST_CAL_ACTION_BEGIN, NULL, idle))
            page(u, ST_CAL_UI_ZERO, 1);
        return false;
    case ST_CAL_UI_ZERO:
        if (u->selection == 1)
            return cancel(u);
        if (send(u, ST_CAL_ACTION_ZERO, NULL, idle))
            page(u, ST_CAL_UI_WAIT_ZERO, 0);
        return false;
    case ST_CAL_UI_WAIT_ZERO:
    case ST_CAL_UI_WAIT_LOAD:
        return cancel(u);
    case ST_CAL_UI_LOAD_KIND:
        if (u->selection == 2)
            return cancel(u);
        page(u, u->selection == 0 ? ST_CAL_UI_PRESETS : ST_CAL_UI_UNITS,
             u->selection == 0 ? PRESET_COUNT : 3);
        return false;
    case ST_CAL_UI_PRESETS:
        if (u->selection == PRESET_COUNT)
        {
            page(u, ST_CAL_UI_LOAD_KIND, 2);
            return false;
        }
        if (!send(u, ST_CAL_ACTION_REFERENCE_CLEAR, NULL, idle))
            return false;
        u->draft = presets[u->selection];
        u->reference_parent = ST_CAL_UI_PRESETS;
        page(u, ST_CAL_UI_REFERENCE, 1);
        return false;
    case ST_CAL_UI_UNITS:
        if (u->selection == 3)
        {
            page(u, ST_CAL_UI_LOAD_KIND, 2);
            return false;
        }
        if (!send(u, ST_CAL_ACTION_REFERENCE_CLEAR, NULL, idle))
            return false;
        u->unit_reset = true;
        u->draft = (st_cal_reference){.unit = (st_cal_unit)u->selection,
                                      .gravity_um_s2 =
                                          u->selection == ST_CAL_NEWTONS ? 0 : ST_CAL_G0_UM_S2};
        u->edit_value = 0;
        page(u, ST_CAL_UI_EDITOR, 0);
        return false; /* Explicit clear; never reinterpret a numeric value in a new unit. */
    case ST_CAL_UI_EDITOR:
        if (u->editing)
        {
            u->editing = false;
            return false;
        }
        if (u->selection < DIGITS)
        {
            u->unit_reset = false;
            u->edit_backup = u->edit_value;
            u->editing = true;
            return false;
        }
        if (u->selection == EDITOR_BACK)
            return back(u);
        u->draft.milli_units = u->edit_value;
        {
            int64_t force;
            if (!st_cal_reference_force(u->draft, &force))
            {
                u->view.error = ST_CAL_REFERENCE;
                u->local_error = ST_CAL_REFERENCE;
                return false;
            }
        }
        u->reference_parent = ST_CAL_UI_EDITOR;
        page(u, ST_CAL_UI_REFERENCE, 1);
        return false;
    case ST_CAL_UI_REFERENCE:
        if (u->selection == 1)
            return back(u);
        if (send(u, ST_CAL_ACTION_REFERENCE, &u->draft, idle))
            page(u, ST_CAL_UI_LOAD, 1);
        return false;
    case ST_CAL_UI_LOAD:
        if (u->selection == 1)
        {
            page(u, ST_CAL_UI_LOAD_KIND, 2);
            return false;
        }
        if (send(u, ST_CAL_ACTION_LOAD, NULL, idle))
            page(u, ST_CAL_UI_WAIT_LOAD, 0);
        return false;
    case ST_CAL_UI_REVIEW:
        if (u->selection == 1)
            return cancel(u);
        if (send(u, ST_CAL_ACTION_SAVE, NULL, idle))
            page(u, ST_CAL_UI_RESULT, 0);
        return false;
    case ST_CAL_UI_RESULT:
        return true;
    default:
        return false;
    }
}
void st_cal_ui_render(st_cal_ui *u, st_cal_panel *p)
{
    refresh(u);
    memset(p, 0, sizeof(*p));
    const char *title = "CALIBRATION";
    switch (u->page)
    {
    case ST_CAL_UI_INTRO:
        (void)snprintf(p->rows[1], 22, "ZERO + KNOWN LOAD");
        (void)snprintf(p->rows[2], 22, "%s",
                       u->view.simulated ? "SIMULATED PROFILE" : "CONFIG BOUND PROFILE");
        (void)snprintf(p->rows[3], 22, "%c BEGIN", u->selection == 0 ? '>' : ' ');
        (void)snprintf(p->rows[4], 22, "%c BACK", u->selection == 1 ? '>' : ' ');
        break;
    case ST_CAL_UI_ZERO:
        title = "REMOVE ALL LOAD";
        (void)snprintf(p->rows[2], 22, "%c CAPTURE ZERO", u->selection == 0 ? '>' : ' ');
        (void)snprintf(p->rows[4], 22, "%c BACK", u->selection == 1 ? '>' : ' ');
        break;
    case ST_CAL_UI_WAIT_ZERO:
    case ST_CAL_UI_WAIT_LOAD:
        title = u->page == ST_CAL_UI_WAIT_ZERO ? "ZERO SAMPLES" : "LOADED SAMPLES";
        (void)snprintf(p->rows[1], 22, "%lu / %lu", (unsigned long)u->view.samples,
                       (unsigned long)u->view.required);
        (void)snprintf(p->rows[4], 22, "> CANCEL");
        break;
    case ST_CAL_UI_LOAD_KIND:
        title = "REFERENCE INPUT";
        (void)snprintf(p->rows[1], 22, "VERIFY ACTUAL LOAD");
        (void)snprintf(p->rows[2], 22, "%c PRESET MASS", u->selection == 0 ? '>' : ' ');
        (void)snprintf(p->rows[3], 22, "%c CUSTOM g/kg/N", u->selection == 1 ? '>' : ' ');
        (void)snprintf(p->rows[4], 22, "%c BACK", u->selection == 2 ? '>' : ' ');
        break;
    case ST_CAL_UI_PRESETS:
    {
        title = "MASS PRESETS";
        unsigned top = u->selection < 3 ? 0 : u->selection - 2;
        for (unsigned row = 0; row < 3 && top + row <= PRESET_COUNT; row++)
        {
            unsigned item = top + row;
            char value[18];
            if (item == PRESET_COUNT)
                (void)snprintf(value, sizeof(value), "BACK");
            else
                value_text(value, sizeof(value), presets[item], false);
            (void)snprintf(p->rows[row + 1], 22, "%c %s", u->selection == item ? '>' : ' ', value);
        }
        (void)snprintf(p->rows[4], 22, "VERIFY ACTUAL MASS");
        break;
    }
    case ST_CAL_UI_UNITS:
        title = "SELECT INPUT UNIT";
        for (unsigned i = 0; i < 3; i++)
            (void)snprintf(p->rows[i + 1], 22, "%c %s", u->selection == i ? '>' : ' ',
                           unit_name((st_cal_unit)i));
        (void)snprintf(p->rows[4], 22, "%c BACK", u->selection == 3 ? '>' : ' ');
        break;
    case ST_CAL_UI_EDITOR:
        title = "CUSTOM VALUE";
        {
            st_cal_reference ref = u->draft;
            ref.milli_units = u->edit_value;
            value_text(p->rows[1], 22, ref, true);
        }
        (void)snprintf(p->rows[2], 22, "%s", u->editing ? "EDIT DIGIT" : "SELECT DIGIT");
        (void)snprintf(p->rows[3], 22, "%c CONFIRM", u->selection == EDITOR_OK ? '>' : ' ');
        (void)snprintf(p->rows[4], 22, "%c BACK", u->selection == EDITOR_BACK ? '>' : ' ');
        if (u->selection < DIGITS)
        {
            p->inverse = true;
            p->inverse_row = 1;
            p->inverse_column = (uint8_t)(u->selection + (u->selection >= 6 ? 1u : 0u));
        }
        if (u->unit_reset)
            (void)snprintf(p->rows[5], 22, "UNIT SET:VALUE CLEAR");
        break;
    case ST_CAL_UI_REFERENCE:
        title = "CONFIRM REFERENCE";
        value_text(p->rows[1], 22, u->draft, false);
        (void)snprintf(p->rows[2], 22, "%s",
                       u->draft.unit == ST_CAL_NEWTONS ? "DIRECT FORCE" : "g=9.80665 m/s2");
        (void)snprintf(p->rows[3], 22, "%c USE VALUE", u->selection == 0 ? '>' : ' ');
        (void)snprintf(p->rows[4], 22, "%c BACK", u->selection == 1 ? '>' : ' ');
        break;
    case ST_CAL_UI_LOAD:
        title = "APPLY KNOWN LOAD";
        value_text(p->rows[1], 22, u->view.reference, false);
        (void)snprintf(p->rows[3], 22, "%c CAPTURE LOAD", u->selection == 0 ? '>' : ' ');
        (void)snprintf(p->rows[4], 22, "%c BACK", u->selection == 1 ? '>' : ' ');
        break;
    case ST_CAL_UI_REVIEW:
        title = "REVIEW CALIBRATION";
        (void)snprintf(p->rows[1], 22, "ZERO %ld", (long)u->view.zero_raw);
        coefficient_text(p->rows[2], u->view.slope_nn);
        (void)snprintf(p->rows[3], 22, "nN/count");
        (void)snprintf(p->rows[4], 22, "%c %s", u->selection == 0 ? '>' : ' ',
                       u->view.storage_bound ? "SAVE NEW" : "SAVE UNBOUND");
        (void)snprintf(p->rows[5], 22, "%c BACK", u->selection == 1 ? '>' : ' ');
        break;
    case ST_CAL_UI_RESULT:
        title = u->view.state == ST_CAL_COMMITTED ? "CALIBRATION SAVED" : "CALIBRATION FAILED";
        (void)snprintf(p->rows[1], 22, "%s",
                       u->view.state == ST_CAL_COMMITTED ? "NEW PROFILE ACCEPTED"
                                                         : "OLD PROFILE RETAINED");
        (void)snprintf(p->rows[4], 22, "> BACK");
        break;
    }
    (void)snprintf(p->rows[0], 22, "%s", title);
    if (u->local_error || u->view.error)
        (void)snprintf(p->rows[5], 22, "%s",
                       error_name(u->local_error ? u->local_error : u->view.error));
    else if (u->page == ST_CAL_UI_INTRO && u->view.configured && !u->view.storage_bound)
        (void)snprintf(p->rows[5], 22, "SAVE UNBOUND");
    else if (u->page == ST_CAL_UI_INTRO && !u->view.configured)
        (void)snprintf(p->rows[5], 22, "SERVICE UNBOUND");
}
