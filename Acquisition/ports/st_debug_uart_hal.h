#ifndef ST_DEBUG_UART_HAL_H
#define ST_DEBUG_UART_HAL_H
#include "st_debug.h"
#if defined(STM32G071xx)
#include "stm32g0xx_hal.h"
#else
#include "stm32f4xx_hal.h"
#endif
typedef struct
{
    UART_HandleTypeDef uart;
    _Atomic(unsigned) tx_state;
    uint8_t rx_byte, rx_queue[128];
    _Atomic(uint32_t) rx_head, rx_tail, rx_dropped;
    bool verified;
} st_debug_uart_hal;
bool st_debug_uart_configure(st_debug_uart_hal *, bool board_verified);
st_debug_port st_debug_uart_port(st_debug_uart_hal *);
void st_debug_uart_tx_complete(st_debug_uart_hal *, UART_HandleTypeDef *);
void st_debug_uart_rx_complete(st_debug_uart_hal *, UART_HandleTypeDef *);
void st_debug_uart_error(st_debug_uart_hal *, UART_HandleTypeDef *);
bool st_debug_uart_read(st_debug_uart_hal *, uint8_t *);
/* USART1_IRQHandler routes HAL_UART_IRQHandler(&port.uart). Callbacks only
 * publish completion/queue RX. No debug command executes acquisition actions. */
#endif
