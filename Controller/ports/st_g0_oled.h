#ifndef ST_G0_OLED_H
#define ST_G0_OLED_H
#include "st_ui.h"
#include "stm32g0xx_hal.h"
typedef struct
{
    I2C_HandleTypeDef *i2c;
    _Atomic(unsigned) state;
    uint32_t (*now_ms)(void);
    uint32_t started_ms;
    uint8_t address;
    bool verified;
} st_g0_oled_port;
/* Stores handles only. No peripheral/pin/clock initialization occurs here. */
void st_g0_oled_init(st_g0_oled_port *, I2C_HandleTypeDef *, uint32_t (*now_ms)(void),
                     bool board_verified);
st_i2c_port st_g0_oled_i2c(st_g0_oled_port *);
void st_g0_oled_complete(st_g0_oled_port *, I2C_HandleTypeDef *);
void st_g0_oled_error(st_g0_oled_port *, I2C_HandleTypeDef *);
#endif
