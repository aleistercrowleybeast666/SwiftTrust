#ifndef ST_F4_CAPTURE_H
#define ST_F4_CAPTURE_H
#include "st_core.h"
#include "st_cal_input.h"
#include "stm32f4xx_hal.h"
typedef struct
{
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    st_logger *logger;
    st_cal_input *calibration;
    bool verified, pending_logging;
    _Atomic(bool) busy;
    volatile uint32_t sequence; /* DRDY ISR-owned counter, sampled by foreground. */
    uint32_t pending_sequence, pending_generation;
    uint64_t pending_time_us;
    uint8_t tx[12], rx[12];
    _Atomic(uint32_t) dma_busy, dma_errors;
} st_f4_capture_port;
/* Initialization stores references only. verified requires actual ADS register
 * readback (24-bit words, CCITT output CRC, mode 1), DMA and clock acceptance.
 * EXTI and SPI completion IRQs must be serialized.
 * Once verified, every DRDY drains the ADC, including IDLE; only frames whose
 * DRDY occurred while logging was enabled enter that recording generation. This does not configure
 * ADC. */
void st_f4_capture_init(st_f4_capture_port *, SPI_HandleTypeDef *, GPIO_TypeDef *, uint16_t,
                        st_logger *, bool verified);
/* Bind once before enabling EXTI; NULL disables the optional calibration tap. */
void st_f4_capture_bind_calibration(st_f4_capture_port *, st_cal_input *);
void st_f4_capture_drdy(st_f4_capture_port *, uint64_t edge_time_us);
void st_f4_capture_complete(st_f4_capture_port *, SPI_HandleTypeDef *);
void st_f4_capture_error(st_f4_capture_port *, SPI_HandleTypeDef *);
typedef struct
{
    GPIO_TypeDef *port[4];
    uint16_t pin[4];
    bool active_high, verified;
} st_f4_led_port;
bool st_f4_voltage_leds(const st_f4_led_port *, st_voltage_view);
#endif
