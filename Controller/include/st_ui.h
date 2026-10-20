#ifndef ST_UI_H
#define ST_UI_H
#include "st_core.h"
#include "st_link2.h"
typedef enum
{
    ST_UI_HOME,
    ST_UI_CAPTURE,
    ST_UI_STATUS,
    ST_UI_SETTINGS,
    ST_UI_CONFIRM,
    ST_UI_IGNITION_ONLY,
    ST_UI_COMBINED
} st_ui_page;
typedef enum
{
    ST_UI_NEXT,
    ST_UI_PREV,
    ST_UI_PRESS,
    ST_UI_BACK
} st_ui_event;
typedef struct
{
    st_ui_page page;
    unsigned selection, return_selection;
    bool unavailable;
} st_ui;
typedef struct
{
    char rows[8][22];
} st_ui_model;
void st_ui_init(st_ui *u);
/* Returns true only when a START/STOP transaction was actually created. */
bool st_ui_handle(st_ui *u, st_ui_event e, st_client *c, uint32_t now_ms);
void st_ui_render(const st_ui *u, const st_client *c, st_voltage_view own, uint32_t now_ms,
                  st_ui_model *m);
/* Active compact RF protocol entry points; legacy functions remain regression references. */
bool st_ui2_handle(st_ui *, st_ui_event, st2_client *, uint32_t now_ms);
void st_ui2_render(const st_ui *, const st2_client *, st_voltage_view, uint32_t now_ms,
                   st_ui_model *);
void st_ui_raster(const st_ui_model *m, uint8_t frame[1024]);
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
} st_button;
bool st_button_press(st_button *b, bool pressed, uint32_t now_ms, uint32_t debounce_ms);

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
