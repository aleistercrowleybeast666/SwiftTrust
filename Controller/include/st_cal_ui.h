#ifndef ST_CAL_UI_H
#define ST_CAL_UI_H
#include "st_cal_service.h"
typedef enum
{
    ST_CAL_UI_NEXT,
    ST_CAL_UI_PREV,
    ST_CAL_UI_PRESS,
    ST_CAL_UI_BACK
} st_cal_ui_event;
typedef enum
{
    ST_CAL_UI_INTRO,
    ST_CAL_UI_ZERO,
    ST_CAL_UI_WAIT_ZERO,
    ST_CAL_UI_LOAD_KIND,
    ST_CAL_UI_PRESETS,
    ST_CAL_UI_UNITS,
    ST_CAL_UI_EDITOR,
    ST_CAL_UI_REFERENCE,
    ST_CAL_UI_LOAD,
    ST_CAL_UI_WAIT_LOAD,
    ST_CAL_UI_REVIEW,
    ST_CAL_UI_RESULT
} st_cal_ui_page;
typedef struct
{
    char rows[ST_OLED_ROWS][ST_UI_ROW_BYTES];
    bool inverse;
    uint8_t inverse_row, inverse_column;
    bool inverse_digit;
} st_cal_panel;
typedef struct st_cal_ui
{
    st_cal_control_port port;
    st_cal_ui_page page, reference_parent;
    st_cal_view view;
    st_cal_reference draft;
    unsigned selection;
    uint32_t edit_value, edit_backup;
    bool editing, unit_reset;
    st_cal_error local_error;
} st_cal_ui;
void st_cal_ui_init(st_cal_ui *, st_cal_control_port);
void st_cal_ui_open(st_cal_ui *);
/* True means exit to parent. All input is rotation + short press; no new key.
 * A backend is optional: unbound service displays unavailable and never saves.
 * Presets are convenience entries, never claims about actual standard masses. */
bool st_cal_ui_handle(st_cal_ui *, st_cal_ui_event, bool capture_idle_and_link_fresh);
void st_cal_ui_render(st_cal_ui *, st_cal_panel *);
#endif
