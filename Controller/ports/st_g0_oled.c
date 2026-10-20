#include "st_g0_oled.h"
#include <string.h>
enum
{
    I2C_FREE,
    I2C_BUSY,
    I2C_DONE,
    I2C_ERROR
};
void st_g0_oled_init(st_g0_oled_port *p, I2C_HandleTypeDef *i2c, uint32_t (*now)(void),
                     bool verified)
{
    memset(p, 0, sizeof(*p));
    p->i2c = i2c;
    p->now_ms = now;
    p->verified = verified && i2c && now;
    atomic_init(&p->state, I2C_FREE);
}
static st_async_result submit(void *ctx, uint8_t address, const uint8_t *data, size_t n)
{
    st_g0_oled_port *p = ctx;
    if (!p->verified || (address != 0x3c && address != 0x3d) || !data || !n || n > 32)
        return ST_IO_ERROR;
    unsigned state = atomic_load_explicit(&p->state, memory_order_acquire);
    if (state == I2C_ERROR)
        return ST_IO_ERROR;
    if (state != I2C_FREE || HAL_I2C_GetState(p->i2c) != HAL_I2C_STATE_READY)
        return ST_IO_WAIT;
    p->address = address;
    p->started_ms = p->now_ms();
    atomic_store_explicit(&p->state, I2C_BUSY, memory_order_release);
    HAL_StatusTypeDef r = HAL_I2C_Master_Transmit_IT(p->i2c, (uint16_t)(address << 1),
                                                     (uint8_t *)(uintptr_t)data, (uint16_t)n);
    if (r != HAL_OK)
    {
        atomic_store_explicit(&p->state, r == HAL_BUSY ? I2C_FREE : I2C_ERROR,
                              memory_order_release);
        return r == HAL_BUSY ? ST_IO_WAIT : ST_IO_ERROR;
    }
    return ST_IO_DONE;
}
static st_async_result poll(void *ctx)
{
    st_g0_oled_port *p = ctx;
    unsigned state = atomic_load_explicit(&p->state, memory_order_acquire);
    if (state == I2C_DONE)
    {
        atomic_store_explicit(&p->state, I2C_FREE, memory_order_release);
        return ST_IO_DONE;
    }
    if (state == I2C_ERROR)
        return ST_IO_ERROR;
    if (state == I2C_BUSY && (uint32_t)(p->now_ms() - p->started_ms) > 100)
    {
        /* Quiesce completion/abort IRQ before explicitly reinitializing this port. */
        atomic_store_explicit(&p->state, I2C_ERROR, memory_order_release);
        (void)HAL_I2C_Master_Abort_IT(p->i2c, (uint16_t)(p->address << 1));
        return ST_IO_ERROR;
    }
    return ST_IO_WAIT;
}
st_i2c_port st_g0_oled_i2c(st_g0_oled_port *p)
{
    return (st_i2c_port){p, submit, poll};
}
void st_g0_oled_complete(st_g0_oled_port *p, I2C_HandleTypeDef *i2c)
{
    if (p->verified && p->i2c == i2c &&
        atomic_load_explicit(&p->state, memory_order_acquire) == I2C_BUSY)
        atomic_store_explicit(&p->state, I2C_DONE, memory_order_release);
}
void st_g0_oled_error(st_g0_oled_port *p, I2C_HandleTypeDef *i2c)
{
    if (p->verified && p->i2c == i2c)
        atomic_store_explicit(&p->state, I2C_ERROR, memory_order_release);
}
