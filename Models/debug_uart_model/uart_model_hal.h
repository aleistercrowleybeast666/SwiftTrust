#ifndef __UART_MODEL_HAL_H
#define __UART_MODEL_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { unsigned id; } GPIO_TypeDef;
typedef struct { unsigned id; } USART_TypeDef;
typedef struct
{
    uint32_t Pin, Mode, Pull, Speed, Alternate;
} GPIO_InitTypeDef;
typedef struct
{
    USART_TypeDef *Instance;
    struct
    {
        uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling;
    } Init;
    uint32_t gState;
} UART_HandleTypeDef;
GPIO_TypeDef *UartModel_GetGpioA(void);
USART_TypeDef *UartModel_GetUsart1(void);
USART_TypeDef *UartModel_GetUsart2(void);
#define GPIOA UartModel_GetGpioA()
#define USART1 UartModel_GetUsart1()
#define USART2 UartModel_GetUsart2()
#define GPIO_PIN_2 (1u << 2)
#define GPIO_PIN_3 (1u << 3)
#define GPIO_PIN_9 (1u << 9)
#define GPIO_PIN_10 (1u << 10)
#define GPIO_MODE_AF_PP 2u
#define GPIO_PULLUP 1u
#define GPIO_SPEED_FREQ_LOW 0u
#define GPIO_AF1_USART1 1u
#define GPIO_AF7_USART1 7u
#define GPIO_AF7_USART2 7u
#define USART1_IRQn 37
#define USART2_IRQn 38
#define UART_WORDLENGTH_8B 8u
#define UART_STOPBITS_1 1u
#define UART_PARITY_NONE 0u
#define UART_MODE_TX_RX 3u
#define UART_HWCONTROL_NONE 0u
#define UART_OVERSAMPLING_16 16u
#define HAL_UART_STATE_READY 32u
#define HAL_UART_STATE_BUSY_TX 33u
void UartModel_EnableClock(unsigned clock);
#define __HAL_RCC_GPIOA_CLK_ENABLE() UartModel_EnableClock(0)
#define __HAL_RCC_USART1_CLK_ENABLE() UartModel_EnableClock(1)
#define __HAL_RCC_USART2_CLK_ENABLE() UartModel_EnableClock(2)
void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
void HAL_NVIC_SetPriority(int, unsigned, unsigned);
void HAL_NVIC_EnableIRQ(int);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
void HAL_UART_IRQHandler(UART_HandleTypeDef *);
#endif
