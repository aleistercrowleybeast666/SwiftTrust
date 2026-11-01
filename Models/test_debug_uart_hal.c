#include "st_debug_uart_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GPIO_TypeDef gpio_a;
static USART_TypeDef usart1, usart2;
GPIO_TypeDef *UartModel_GetGpioA(void) { return &gpio_a; }
USART_TypeDef *UartModel_GetUsart1(void) { return &usart1; }
USART_TypeDef *UartModel_GetUsart2(void) { return &usart2; }
static st_debug_uart_hal uart;
static unsigned checks, clocks[3], gpio_calls, init_calls, rx_calls, tx_calls, irq_calls;
static GPIO_InitTypeDef configured_gpio;
static int configured_irq, enabled_irq;
static unsigned configured_priority;
static HAL_StatusTypeDef init_result, rx_result, tx_result;
static uint8_t *rx_buffer, *tx_buffer;
static uint16_t tx_length;
static unsigned irq_event;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr, "UART line%d: %s\n", __LINE__, #x); exit(1); } } while (0)

#include "board_uart_callbacks.inc"

void UartModel_EnableClock(unsigned clock)
{
    C(clock < 3u);
    ++clocks[clock];
}
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{
    C(port == GPIOA);
    configured_gpio = *config;
    ++gpio_calls;
}
void HAL_NVIC_SetPriority(int irq, unsigned priority, unsigned subpriority)
{
    C(subpriority == 0u);
    configured_irq = irq;
    configured_priority = priority;
}
void HAL_NVIC_EnableIRQ(int irq) { enabled_irq = irq; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *handle)
{
    C(handle == &uart.uart);
    ++init_calls;
    handle->gState = HAL_UART_STATE_READY;
    return init_result;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *handle, uint8_t *data, uint16_t count)
{
    C(handle == &uart.uart && data == &uart.rx_byte && count == 1u);
    ++rx_calls;
    rx_buffer = data;
    return rx_result;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *handle, uint8_t *data, uint16_t count)
{
    C(handle == &uart.uart && count > 0u && count <= 64u);
    ++tx_calls;
    if (tx_result == HAL_OK)
    {
        tx_buffer = data;
        tx_length = count;
        handle->gState = HAL_UART_STATE_BUSY_TX;
    }
    return tx_result;
}
void HAL_UART_IRQHandler(UART_HandleTypeDef *handle)
{
    C(handle == &uart.uart);
    ++irq_calls;
    if (irq_event == 1u)
    {
        handle->gState = HAL_UART_STATE_READY;
        HAL_UART_TxCpltCallback(handle);
    }
    else if (irq_event == 2u)
        HAL_UART_RxCpltCallback(handle);
    else if (irq_event == 3u)
        HAL_UART_ErrorCallback(handle);
}
static void UartModel_Reset(void)
{
    memset(clocks, 0, sizeof clocks);
    gpio_calls = init_calls = rx_calls = tx_calls = irq_calls = 0;
    configured_irq = enabled_irq = -1;
    configured_priority = UINT32_MAX;
    init_result = rx_result = tx_result = HAL_OK;
    rx_buffer = tx_buffer = NULL;
    tx_length = 0;
    irq_event = 0;
}
static void UartModel_Configure(void)
{
    UartModel_Reset();
    C(st_debug_uart_configure(&uart, true));
    C(uart.verified && gpio_calls == 1u && init_calls == 1u && rx_calls == 1u);
    C(clocks[0] == 1u);
#if defined(STM32G071xx)
    C(clocks[1] == 1u && clocks[2] == 0u && uart.uart.Instance == USART1);
    C(configured_gpio.Pin == (GPIO_PIN_9 | GPIO_PIN_10));
    C(configured_gpio.Alternate == GPIO_AF1_USART1);
    C(configured_irq == USART1_IRQn && enabled_irq == USART1_IRQn && configured_priority == 3u);
#else
    C(clocks[1] == 0u && clocks[2] == 1u && uart.uart.Instance == USART2);
    C(configured_gpio.Pin == (GPIO_PIN_2 | GPIO_PIN_3));
    C(configured_gpio.Alternate == GPIO_AF7_USART2);
    C(configured_irq == USART2_IRQn && enabled_irq == USART2_IRQn && configured_priority == 14u);
#endif
    C(configured_gpio.Mode == GPIO_MODE_AF_PP && configured_gpio.Pull == GPIO_PULLUP &&
      configured_gpio.Speed == GPIO_SPEED_FREQ_LOW);
    C(uart.uart.Init.BaudRate == 115200u && uart.uart.Init.WordLength == UART_WORDLENGTH_8B &&
      uart.uart.Init.StopBits == UART_STOPBITS_1 && uart.uart.Init.Parity == UART_PARITY_NONE &&
      uart.uart.Init.Mode == UART_MODE_TX_RX && uart.uart.Init.HwFlowCtl == UART_HWCONTROL_NONE &&
      uart.uart.Init.OverSampling == UART_OVERSAMPLING_16);
}
static void UartModel_Receive(uint8_t value)
{
    C(rx_buffer == &uart.rx_byte);
    *rx_buffer = value;
    irq_event = 2;
    UART_MODEL_IRQ();
}
static void UartModel_Complete(void)
{
    irq_event = 1;
    UART_MODEL_IRQ();
}
static void UartModel_CheckTransmit(void)
{
    UartModel_Configure();
    st_debug_port port = st_debug_uart_port(&uart);
    UART_HandleTypeDef unrelated = {0};
    uint8_t data[65] = {42};
    C(port.poll(port.ctx) == ST_IO_WAIT);
    HAL_UART_TxCpltCallback(&uart.uart); /* A stray callback cannot complete a free port. */
    C(port.poll(port.ctx) == ST_IO_WAIT);
    C(port.submit(port.ctx, NULL, 1) == ST_IO_ERROR);
    C(port.submit(port.ctx, data, 0) == ST_IO_ERROR);
    C(port.submit(port.ctx, data, sizeof data) == ST_IO_ERROR && tx_calls == 0u);
    uart.uart.gState = HAL_UART_STATE_BUSY_TX;
    C(port.submit(port.ctx, data, 1) == ST_IO_WAIT && tx_calls == 0u);
    uart.uart.gState = HAL_UART_STATE_READY;
    tx_result = HAL_BUSY;
    C(port.submit(port.ctx, data, 1) == ST_IO_WAIT);
    tx_result = HAL_OK;
    C(port.submit(port.ctx, data, 64) == ST_IO_DONE);
    C(tx_buffer == data && tx_length == 64u && port.poll(port.ctx) == ST_IO_WAIT);
    HAL_UART_TxCpltCallback(&unrelated);
    HAL_UART_ErrorCallback(&unrelated);
    C(port.poll(port.ctx) == ST_IO_WAIT);
    C(port.submit(port.ctx, data, 1) == ST_IO_WAIT);
    UartModel_Complete();
    C(port.submit(port.ctx, data, 1) == ST_IO_WAIT); /* poll owns buffer release */
    C(port.poll(port.ctx) == ST_IO_DONE && port.poll(port.ctx) == ST_IO_WAIT);
    C(port.submit(port.ctx, data, 1) == ST_IO_DONE);
    irq_event = 3;
    UART_MODEL_IRQ();
    UartModel_Complete(); /* A late completion must not erase an earlier error. */
    C(port.poll(port.ctx) == ST_IO_ERROR && port.submit(port.ctx, data, 1) == ST_IO_ERROR);
    UartModel_Configure();
    tx_result = HAL_ERROR;
    C(port.submit(port.ctx, data, 1) == ST_IO_ERROR);
    UartModel_Complete();
    C(port.poll(port.ctx) == ST_IO_ERROR);
}
static void UartModel_CheckReceive(void)
{
    UartModel_Configure();
    uint8_t value = 0;
    UART_HandleTypeDef unrelated = {0};
    HAL_UART_RxCpltCallback(&unrelated);
    C(rx_calls == 1u && !st_debug_uart_read(&uart, &value));
    for (unsigned i = 0; i < 128u; ++i)
        UartModel_Receive((uint8_t)i);
    C(atomic_load(&uart.rx_dropped) == 0u && rx_calls == 129u);
    UartModel_Receive(200);
    C(atomic_load(&uart.rx_dropped) == 1u);
    atomic_store(&uart.rx_dropped, UINT32_MAX);
    UartModel_Receive(201);
    C(atomic_load(&uart.rx_dropped) == UINT32_MAX);
    for (unsigned i = 0; i < 128u; ++i)
        C(st_debug_uart_read(&uart, &value) && value == (uint8_t)i);
    C(!st_debug_uart_read(&uart, &value));
    atomic_store(&uart.rx_head, UINT32_MAX - 1u);
    atomic_store(&uart.rx_tail, UINT32_MAX - 1u);
    for (unsigned i = 0; i < 4u; ++i)
        UartModel_Receive((uint8_t)(77u + i));
    for (unsigned i = 0; i < 4u; ++i)
        C(st_debug_uart_read(&uart, &value) && value == (uint8_t)(77u + i));
    C(!st_debug_uart_read(&uart, &value));
    st_debug_port port = st_debug_uart_port(&uart);
    uint8_t data = 1;
    C(port.submit(port.ctx, &data, 1) == ST_IO_DONE);
    rx_result = HAL_ERROR;
    UartModel_Receive(99);
    UartModel_Complete();
    C(port.poll(port.ctx) == ST_IO_ERROR);
    C(st_debug_uart_read(&uart, &value) && value == 99u);
}
static void UartModel_CheckRetainedBuffer(void)
{
    UartModel_Configure();
    st_debug debug;
    uint8_t data[128], snapshot[64];
    for (unsigned i = 0; i < sizeof data; ++i)
        data[i] = (uint8_t)i;
    st_debug_init(&debug, st_debug_uart_port(&uart));
    C(st_debug_write(&debug, data, sizeof data, 0));
    st_debug_service(&debug);
    C(debug.inflight && tx_buffer == debug.tx && tx_length == 64u && tx_calls == 1u);
    memcpy(snapshot, tx_buffer, sizeof snapshot);
    memset(data, 0xee, sizeof data);
    C(st_debug_write(&debug, data, sizeof data, 0));
    st_debug_service(&debug);
    C(tx_calls == 1u && memcmp(snapshot, tx_buffer, sizeof snapshot) == 0);
    UartModel_Complete();
    st_debug_service(&debug);
    C(tx_calls == 2u && tx_buffer[0] == 64u && tx_buffer[63] == 127u);
    UartModel_Complete();
    st_debug_service(&debug);
    C(tx_calls == 3u && tx_buffer[0] == 0xeeu);
    irq_event = 3;
    UART_MODEL_IRQ();
    UartModel_Complete();
    st_debug_service(&debug);
    C(debug.failed && !debug.inflight && tx_calls == 3u);
}
int main(void)
{
    UartModel_Reset();
    C(!st_debug_uart_configure(&uart, false));
    C(!uart.verified && !gpio_calls && !init_calls && !rx_calls && !clocks[0]);
    UartModel_Reset();
    init_result = HAL_ERROR;
    C(!st_debug_uart_configure(&uart, true) && !uart.verified && !rx_calls && enabled_irq == -1);
    UartModel_Reset();
    rx_result = HAL_ERROR;
    C(!st_debug_uart_configure(&uart, true) && !uart.verified);
    UartModel_CheckTransmit();
    UartModel_CheckReceive();
    UartModel_CheckRetainedBuffer();
#if defined(STM32G071xx)
    const char *board = "G071 USART1 PA9/PA10 AF1";
#else
    const char *board = "F411 USART2 PA2/PA3 AF7";
#endif
    printf("PASS actual debug UART HAL + board IRQ (%s): %u assertions; init, TX/RX, ring overflow/wrap, retained buffer, sticky errors. Host HAL model only.\n", board, checks);
    return 0;
}
