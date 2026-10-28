#ifndef ST_UI_H
#define ST_UI_H
#include "st_core.h"
#include "st_link2.h"
#include "st_cal_ui.h"
typedef enum
{
    ST_UI_HOME,
    ST_UI_CAPTURE,
    ST_UI_STATUS,
    ST_UI_SETTINGS,
    ST_UI_CONFIRM,
    ST_UI_IGNITION_ONLY,
    ST_UI_COMBINED,
    ST_UI_CALIBRATION
} st_ui_page;
typedef enum
{
    ST_UI_NEXT,
    ST_UI_PREV,
    ST_UI_PRESS,
    ST_UI_BACK
} st_ui_event;
typedef enum
{
    ST_UI_NOTICE_NONE,
    ST_UI_NOTICE_BUSY,
    ST_UI_NOTICE_LINK,
    ST_UI_NOTICE_STATE,
    ST_UI_NOTICE_RECONNECT,
    ST_UI_NOTICE_CONNECT
} st_ui_notice;
typedef struct
{
    st_ui_page page;
    unsigned selection;
    unsigned top;
    st_ui_page parent;
    StTaskMode mode;
    uint8_t confirm_action;
    bool await_arm;
    uint32_t ignition_target;
    uint32_t ignition_poll_ms;
    uint32_t sensor_poll_ms;
    st_ui_notice notice;
    st_cal_ui *calibration;
} st_ui;
typedef struct
{
    char rows[ST_OLED_ROWS][ST_UI_ROW_BYTES];
    bool inverse;
    uint8_t inverse_row, inverse_column;
    bool inverse_digit;
} st_ui_model;
void st_ui_init(st_ui *u);
void st_ui_attach_calibration(st_ui *, st_cal_ui *);
/* Active compact RF entry points; true also covers queued STOP intent and a
 * created ignition request. real output remains disabled pending review. */
bool st_ui2_handle(st_ui *, st_ui_event, st2_client *, uint32_t now_ms);
void st_ui2_service(st_ui *, st2_client *, uint32_t now_ms);
/* Voltage argument is the local PA3 battery measurement, never MCU VDDA. */
void st_ui2_render(const st_ui *, const st2_client *, st_voltage_view, uint32_t now_ms,
                   st_ui_model *);
void st_ui_raster(const st_ui_model *m, uint8_t frame[1024]);
typedef enum
{
    UI_TEXT_OK,
    UI_TEXT_INVALID,
    UI_TEXT_MISSING_GLYPH,
    UI_TEXT_TOO_WIDE
} UiTextResult;
UiTextResult Ui_ValidateText(const char *, unsigned *pixels);
typedef struct
{
    uint16_t counter;
    int32_t remainder;
    bool initialized;
} st_encoder;
int32_t st_encoder_update(st_encoder *e, uint16_t counter, unsigned edges_per_detent);
typedef struct
{
    bool raw, stable, initialized;
    uint32_t changed_ms;
    uint32_t pressed_ms;
    bool long_sent, boot_held;
} st_button;
bool st_button_press(st_button *b, bool pressed, uint32_t now_ms, uint32_t debounce_ms);
typedef enum
{
    ST_BUTTON_NONE,
    ST_BUTTON_SHORT,
    ST_BUTTON_LONG
} StButtonEvent;
StButtonEvent Button_Update(st_button *, bool pressed, uint32_t now_ms);
const char *Ui_Text(const char *chinese, const char *english);
unsigned Ui_GetItemCount(const st_ui *);

typedef struct
{
    void *ctx;
    /* Seven-bit address; retain tx bytes until poll completes. No blocking waits. */
    st_async_result (*submit)(void *, uint8_t address, const uint8_t *, size_t);
    st_async_result (*poll)(void *);
} st_i2c_port;
typedef enum
{
    ST_OLED_SSD1306,
    ST_OLED_SH1106
} st_oled_kind;
typedef struct
{
    st_oled_kind kind;
    uint8_t address, column_offset;
} st_oled_config;
typedef struct
{
    st_i2c_port port;
    st_oled_config config;
    uint8_t frame[1024], tx[32];
    uint32_t power_on_ms;
    unsigned phase, offset;
    bool inflight, on, failed;
} st_oled;
/* Existing arm-project display specification: SSD1306 128x64, 7-bit 0x3C.
 * Common 1.3-inch module; exact seller/lot remains a hardware check.
 * Hardware port must establish stable 3.3V and module power/reset sequence before this call. */
void st_oled_init_profile(st_oled *o, st_i2c_port port, st_oled_config config,
                          uint32_t power_on_ms);
void st_oled_init(st_oled *o, st_i2c_port port, uint32_t power_on_ms);
bool st_oled_present(st_oled *o, const uint8_t frame[1024]);
/* One asynchronous I2C transaction at most per call; 16 data bytes per chunk. */
void st_oled_service(st_oled *o, uint32_t now_ms);
#endif
