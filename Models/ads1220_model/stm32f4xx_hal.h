#ifndef __STM32F4XX_HAL_H
#define __STM32F4XX_HAL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct {unsigned id;} GPIO_TypeDef;
typedef struct {unsigned id;} SPI_TypeDef;
typedef struct
{
    uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, FirstBit, NSS;
    uint32_t CRCCalculation, BaudRatePrescaler;
} SPI_InitTypeDef;
typedef struct
{
    SPI_TypeDef *Instance;
    void *hdmarx, *hdmatx;
    SPI_InitTypeDef Init;
    unsigned State;
} SPI_HandleTypeDef;
typedef enum {HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET, GPIO_PIN_SET} GPIO_PinState;
extern SPI_TypeDef AdsTest_Spi1;
#define SPI1 (&AdsTest_Spi1)
#define SPI_MODE_MASTER 1u
#define SPI_DIRECTION_2LINES 2u
#define SPI_DATASIZE_8BIT 8u
#define SPI_POLARITY_LOW 0u
#define SPI_PHASE_2EDGE 1u
#define SPI_FIRSTBIT_MSB 0u
#define SPI_NSS_SOFT 1u
#define SPI_CRCCALCULATION_DISABLE 0u
#define SPI_BAUDRATEPRESCALER_16 0x18u
#define SPI_BAUDRATEPRESCALER_32 0x20u
#define HAL_SPI_STATE_READY 1u
#define HAL_SPI_STATE_BUSY_TX_RX 2u
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *, uint8_t *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *);
HAL_StatusTypeDef HAL_SPI_Abort_IT(SPI_HandleTypeDef *);
uint32_t HAL_RCC_GetPCLK2Freq(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
void AdsTest_Nop(void);
#define __NOP() AdsTest_Nop()
#endif
