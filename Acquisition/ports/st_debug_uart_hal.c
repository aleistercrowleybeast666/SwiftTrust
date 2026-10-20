#include "st_debug_uart_hal.h"
#include <string.h>
enum
{
    UART_FREE,
    UART_BUSY,
    UART_DONE,
    UART_FAILED
};
bool st_debug_uart_configure(st_debug_uart_hal *p, bool verified)
{
    memset(p, 0, sizeof(*p));
    atomic_init(&p->tx_state, UART_FREE);
    atomic_init(&p->rx_head, 0);
    atomic_init(&p->rx_tail, 0);
    atomic_init(&p->rx_dropped, 0);
    p->verified = verified;
    if (!verified)
        return false;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
#if defined(STM32G071xx)
    gpio.Alternate = GPIO_AF1_USART1;
#else
    gpio.Alternate = GPIO_AF7_USART1;
#endif
    HAL_GPIO_Init(GPIOA, &gpio);
    p->uart.Instance = USART1;
    p->uart.Init.BaudRate = 115200;
    p->uart.Init.WordLength = UART_WORDLENGTH_8B;
    p->uart.Init.StopBits = UART_STOPBITS_1;
    p->uart.Init.Parity = UART_PARITY_NONE;
    p->uart.Init.Mode = UART_MODE_TX_RX;
    p->uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    p->uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&p->uart) != HAL_OK)
    {
        p->verified = false;
        return false;
    }
#if defined(STM32G071xx)
    HAL_NVIC_SetPriority(USART1_IRQn, 3, 0);
#else
    HAL_NVIC_SetPriority(USART1_IRQn, 14, 0);
#endif
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    if (HAL_UART_Receive_IT(&p->uart, &p->rx_byte, 1) != HAL_OK)
    {
        p->verified = false;
        return false;
    }
    return true;
}
static st_async_result submit(void *ctx, const uint8_t *data, size_t n)
{
    st_debug_uart_hal *p = ctx;
    if (!p->verified || !data || !n || n > 64)
        return ST_IO_ERROR;
    unsigned state = atomic_load_explicit(&p->tx_state, memory_order_acquire);
    if (state == UART_FAILED)
        return ST_IO_ERROR;
    if (state != UART_FREE || p->uart.gState != HAL_UART_STATE_READY)
        return ST_IO_WAIT;
    atomic_store_explicit(&p->tx_state, UART_BUSY, memory_order_release);
    HAL_StatusTypeDef s = HAL_UART_Transmit_IT(&p->uart, (uint8_t *)(uintptr_t)data, (uint16_t)n);
    if (s == HAL_OK)
        return ST_IO_DONE;
    atomic_store_explicit(&p->tx_state, s == HAL_BUSY ? UART_FREE : UART_FAILED,
                          memory_order_release);
    return s == HAL_BUSY ? ST_IO_WAIT : ST_IO_ERROR;
}
static st_async_result poll(void *ctx)
{
    st_debug_uart_hal *p = ctx;
    unsigned s = atomic_load_explicit(&p->tx_state, memory_order_acquire);
    if (s == UART_FAILED)
        return ST_IO_ERROR;
    if (s != UART_DONE)
        return ST_IO_WAIT;
    atomic_store_explicit(&p->tx_state, UART_FREE, memory_order_release);
    return ST_IO_DONE;
}
st_debug_port st_debug_uart_port(st_debug_uart_hal *p)
{
    return (st_debug_port){p, submit, poll};
}
void st_debug_uart_tx_complete(st_debug_uart_hal *p, UART_HandleTypeDef *u)
{
    if (p->verified && u == &p->uart)
        atomic_store_explicit(&p->tx_state, UART_DONE, memory_order_release);
}
void st_debug_uart_rx_complete(st_debug_uart_hal *p, UART_HandleTypeDef *u)
{
    if (!p->verified || u != &p->uart)
        return;
    uint32_t h = atomic_load_explicit(&p->rx_head, memory_order_relaxed),
             t = atomic_load_explicit(&p->rx_tail, memory_order_acquire);
    if (h - t < 128)
    {
        p->rx_queue[h & 127u] = p->rx_byte;
        atomic_store_explicit(&p->rx_head, h + 1, memory_order_release);
    }
    else
    {
        uint32_t d = atomic_load_explicit(&p->rx_dropped, memory_order_relaxed);
        if (d != UINT32_MAX)
            atomic_store_explicit(&p->rx_dropped, d + 1, memory_order_relaxed);
    }
    if (HAL_UART_Receive_IT(u, &p->rx_byte, 1) != HAL_OK)
        atomic_store_explicit(&p->tx_state, UART_FAILED, memory_order_release);
}
void st_debug_uart_error(st_debug_uart_hal *p, UART_HandleTypeDef *u)
{
    if (p->verified && u == &p->uart)
        atomic_store_explicit(&p->tx_state, UART_FAILED, memory_order_release);
}
bool st_debug_uart_read(st_debug_uart_hal *p, uint8_t *v)
{
    uint32_t t = atomic_load_explicit(&p->rx_tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&p->rx_head, memory_order_acquire))
        return false;
    *v = p->rx_queue[t & 127u];
    atomic_store_explicit(&p->rx_tail, t + 1, memory_order_release);
    return true;
}
