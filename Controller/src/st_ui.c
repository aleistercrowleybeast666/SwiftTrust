#include "st_ui.h"
#include <string.h>
const char *Ui_Text(const char *chinese, const char *english)
{
    return ST_UI_CHINESE ? chinese : english;
}
void st_ui_init(st_ui *ui)
{
    *ui = (st_ui){.page = ST_UI_HOME, .mode = ST_TASK_CAPTURE};
}
void st_ui_attach_calibration(st_ui *ui, st_cal_ui *cal)
{
    ui->calibration = cal;
}
int32_t st_encoder_update(st_encoder *e, uint16_t counter, unsigned edges)
{
    if (!edges || edges > 16)
        return 0;
    if (!e->initialized)
    {
        e->counter = counter;
        e->initialized = true;
        return 0;
    }
    uint16_t raw = (uint16_t)(counter - e->counter);
    e->counter = counter;
    int32_t delta = raw < 32768 ? (int32_t)raw : (int32_t)raw - 65536;
    e->remainder += delta;
    int32_t steps = e->remainder / (int32_t)edges;
    e->remainder %= (int32_t)edges;
    return steps;
}
bool st_button_press(st_button *b, bool pressed, uint32_t now, uint32_t debounce)
{
    /* Boot-held input is ignored until released, then a new debounced press. */
    if (!b->initialized)
    {
        *b = (st_button){.raw = pressed, .stable = pressed, .initialized = true, .changed_ms = now};
        return false;
    }
    if (pressed != b->raw)
    {
        b->raw = pressed;
        b->changed_ms = now;
    }
    if (b->stable != b->raw && (uint32_t)(now - b->changed_ms) >= debounce)
    {
        b->stable = b->raw;
        return b->stable;
    }
    return false;
}

StButtonEvent Button_Update(st_button *button, bool pressed, uint32_t now)
{
    if (!button->initialized)
    {
        *button = (st_button){.raw = pressed,
                              .stable = pressed,
                              .initialized = true,
                              .changed_ms = now,
                              .pressed_ms = now,
                              .boot_held = pressed};
        return ST_BUTTON_NONE;
    }
    if (pressed != button->raw)
    {
        button->raw = pressed;
        button->changed_ms = now;
    }
    if (button->raw != button->stable &&
        (uint32_t)(now - button->changed_ms) >= ST_BUTTON_DEBOUNCE_MS)
    {
        button->stable = button->raw;
        if (button->stable)
        {
            button->pressed_ms = now;
            button->long_sent = false;
        }
        else
        {
            bool short_press = !button->long_sent && !button->boot_held;
            button->boot_held = false;
            button->long_sent = false;
            if (short_press)
                return ST_BUTTON_SHORT;
        }
    }
    if (button->stable && !button->boot_held && !button->long_sent &&
        (uint32_t)(now - button->pressed_ms) >= ST_BUTTON_LONG_MS)
    {
        button->long_sent = true;
        return ST_BUTTON_LONG;
    }
    return ST_BUTTON_NONE;
}
