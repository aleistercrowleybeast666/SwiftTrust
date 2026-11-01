/* Interface-only model for the extracted, unmodified production functions. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef enum { ADS1220_OK, ADS1220_BUSY, ADS1220_ERROR } Ads1220Result;
typedef int IRQn_Type;
typedef struct { unsigned id; } GPIO_TypeDef;
typedef struct { unsigned id; } SPI_TypeDef;
typedef struct { unsigned id; } DMA_Stream_TypeDef;
typedef struct { DMA_Stream_TypeDef *Instance; } DMA_HandleTypeDef;
typedef struct
{
    SPI_TypeDef *Instance;
    struct
    {
        uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS;
        uint32_t BaudRatePrescaler, FirstBit, TIMode, CRCCalculation;
    } Init;
    DMA_HandleTypeDef *hdmarx, *hdmatx;
} SPI_HandleTypeDef;
typedef struct { unsigned id; } TIM_TypeDef;
typedef struct
{
    TIM_TypeDef *Instance;
    struct { uint32_t Prescaler, CounterMode, Period, ClockDivision; } Init;
} TIM_HandleTypeDef;
typedef struct { unsigned id; } ADC_TypeDef;
typedef struct
{
    ADC_TypeDef *Instance;
    struct
    {
        uint32_t ClockPrescaler, Resolution, ScanConvMode, ContinuousConvMode;
        uint32_t DiscontinuousConvMode, ExternalTrigConvEdge, ExternalTrigConv;
        uint32_t DataAlign, NbrOfConversion, DMAContinuousRequests, EOCSelection;
    } Init;
} ADC_HandleTypeDef;
typedef struct { uint32_t Channel, Rank, SamplingTime; } ADC_ChannelConfTypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct
{
    GPIO_TypeDef *port[4];
    uint16_t pin[4];
    bool active_high, verified;
} st_f4_led_port;
typedef struct { uint8_t bytes[12]; uint64_t time_us; } st_capture;
typedef struct { unsigned flags; } st_adc_sample;
static GPIO_TypeDef gpio_a = {0}, gpio_b = {1}, gpio_c = {2}, gpio_d = {3};
static SPI_TypeDef spi1 = {1}, spi2 = {2};
static DMA_Stream_TypeDef dma0, dma5;
static TIM_TypeDef tim5;
static ADC_TypeDef adc1;
#define GPIOA (&gpio_a)
#define GPIOB (&gpio_b)
#define GPIOC (&gpio_c)
#define GPIOD (&gpio_d)
#define SPI1 (&spi1)
#define SPI2 (&spi2)
#define DMA2_Stream0 (&dma0)
#define DMA2_Stream5 (&dma5)
#define TIM5 (&tim5)
#define ADC1 (&adc1)
#define GPIO_PIN_0 (1u << 0)
#define GPIO_PIN_1 (1u << 1)
#define GPIO_PIN_2 (1u << 2)
#define GPIO_PIN_3 (1u << 3)
#define GPIO_PIN_4 (1u << 4)
#define GPIO_PIN_5 (1u << 5)
#define GPIO_PIN_6 (1u << 6)
#define GPIO_PIN_7 (1u << 7)
#define GPIO_PIN_8 (1u << 8)
#define GPIO_PIN_9 (1u << 9)
#define GPIO_PIN_10 (1u << 10)
#define GPIO_PIN_11 (1u << 11)
#define GPIO_PIN_12 (1u << 12)
#define GPIO_PIN_13 (1u << 13)
#define GPIO_PIN_14 (1u << 14)
#define GPIO_PIN_15 (1u << 15)
#define GPIO_MODE_OUTPUT_PP 1u
#define GPIO_MODE_INPUT 2u
#define GPIO_MODE_ANALOG 3u
#define GPIO_MODE_IT_FALLING 4u
#define GPIO_MODE_IT_RISING 5u
#define GPIO_MODE_AF_PP 6u
#define GPIO_NOPULL 0u
#define GPIO_PULLUP 1u
#define GPIO_SPEED_FREQ_VERY_HIGH 3u
#define GPIO_AF5_SPI1 5u
#define GPIO_AF5_SPI2 5u
#define DMA_CHANNEL_3 3u
#define DMA_PERIPH_TO_MEMORY 0u
#define DMA_MEMORY_TO_PERIPH 1u
#define SPI_MODE_MASTER 1u
#define SPI_DIRECTION_2LINES 2u
#define SPI_DATASIZE_8BIT 8u
#define SPI_POLARITY_LOW 0u
#define SPI_PHASE_1EDGE 0u
#define SPI_PHASE_2EDGE 1u
#define SPI_NSS_SOFT 1u
#define SPI_BAUDRATEPRESCALER_16 16u
#define SPI_BAUDRATEPRESCALER_32 32u
#define SPI_FIRSTBIT_MSB 0u
#define SPI_TIMODE_DISABLE 0u
#define SPI_CRCCALCULATION_DISABLE 0u
#define TIM_COUNTERMODE_UP 0u
#define TIM_CLOCKDIVISION_DIV1 0u
#define ADC_CLOCK_SYNC_PCLK_DIV4 4u
#define ADC_RESOLUTION_12B 12u
#define DISABLE 0u
#define ADC_EXTERNALTRIGCONVEDGE_NONE 0u
#define ADC_SOFTWARE_START 1u
#define ADC_DATAALIGN_RIGHT 0u
#define ADC_EOC_SINGLE_CONV 1u
#define ADC_CHANNEL_VREFINT 17u
#define ADC_SAMPLETIME_480CYCLES 480u
#define IGNITION_OUTPUT_OK 0u
#define EXTI15_10_IRQn 40
#define EXTI9_5_IRQn 23
#define DMA2_Stream0_IRQn 56
#define DMA2_Stream5_IRQn 68
#define SPI1_IRQn 35
#define ST_ADC_RATE_WINDOW_US 500000u
#define ST_ADC_FRESH_US 10000u
enum { ST_IDLE, ST_STARTING, ST_CAPTURING, ST_FAULT };
static SPI_HandleTypeDef ads_spi, rf_spi;
static DMA_HandleTypeDef ads_rx, ads_tx;
static TIM_HandleTypeDef timer;
static ADC_HandleTypeDef vref_adc;
static struct { uint32_t drdy_edges; } capture;
static struct { unsigned state; _Atomic(bool) capture_enabled; } logger;
static struct { struct { int input; } endpoint; } calibration;
static int ignition, debug, radio;
static volatile st_capture latest;
static volatile uint32_t latest_version;
static uint32_t seen_version, rate_sequence, measured_rate_millihz;
static uint64_t last_valid_us, rate_time_us;
static bool capture_owner, source_fault, nv_capture_paused, rate_ok, rate_checked;
static bool app_ready, servicing, clock_fault;

static unsigned checks, order, clocks, irq_enables[80], irq_disables[80], priorities[80];
static struct
{
    unsigned init_count, init_order, write_order;
    uint32_t mode, pull, alternate;
    GPIO_PinState value;
} pin_state[4][16];
static GPIO_PinState card_level;
static bool idle_allowed;
static unsigned quiesce_calls, busy_remaining, timeout_calls, waits, resume_calls;
static Ads1220Result quiesce_result, timeout_result, resume_result;
static uint64_t now_us, time_step;
static uint32_t primask, cleared_pin;
static unsigned clear_order, enable_order, drdy_calls, radio_calls, exti_calls;
static unsigned stop_calls, source_errors, cal_errors, service_calls, ignition_calls;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr, "BOARD line%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() (clocks |= 1u)
#define __HAL_RCC_GPIOB_CLK_ENABLE() (clocks |= 2u)
#define __HAL_RCC_GPIOC_CLK_ENABLE() (clocks |= 4u)
#define __HAL_RCC_GPIOD_CLK_ENABLE() (clocks |= 8u)
#define __HAL_RCC_SPI1_CLK_ENABLE() (clocks |= 16u)
#define __HAL_RCC_SPI2_CLK_ENABLE() (clocks |= 32u)
#define __HAL_RCC_DMA2_CLK_ENABLE() (clocks |= 64u)
#define __HAL_LINKDMA(handle, field, stream) ((handle)->field = &(stream))
static void BoardModel_ClearExti(uint32_t pin)
{
    C(primask == 1u);
    cleared_pin = pin;
    clear_order = ++order;
}
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) BoardModel_ClearExti(pin)
static uint32_t __get_PRIMASK(void) { return primask; }
static void __disable_irq(void) { primask = 1; }
static void __set_PRIMASK(uint32_t mask) { primask = mask; }
static void __WFI(void) { ++waits; }
static void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{
    C(port->id < 4u && config->Pin < (1u << 16));
    for (unsigned i = 0; i < 16u; ++i)
        if (config->Pin & (1u << i))
        {
            ++pin_state[port->id][i].init_count;
            pin_state[port->id][i].init_order = ++order;
            pin_state[port->id][i].mode = config->Mode;
            pin_state[port->id][i].pull = config->Pull;
            pin_state[port->id][i].alternate = config->Alternate;
        }
}
static void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t mask, GPIO_PinState value)
{
    for (unsigned i = 0; i < 16u; ++i)
        if (mask & (1u << i))
        {
            pin_state[port->id][i].value = value;
            pin_state[port->id][i].write_order = ++order;
        }
}
static GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    C(port == GPIOA && pin == GPIO_PIN_9);
    return card_level;
}
static void HAL_NVIC_SetPriority(IRQn_Type n, unsigned priority, unsigned subpriority)
{
    C(n >= 0 && n < 80 && subpriority == 0u);
    priorities[n] = priority;
}
static void HAL_NVIC_EnableIRQ(IRQn_Type n)
{
    C(n >= 0 && n < 80);
    ++irq_enables[n];
    enable_order = ++order;
}
static void HAL_NVIC_DisableIRQ(IRQn_Type n)
{
    C(n == EXTI15_10_IRQn);
    ++irq_disables[n];
}
static void fatal(void) { C(false); }
static void need(HAL_StatusTypeDef result) { C(result == HAL_OK); }
static unsigned IgnitionPulse_Init(void) { ++ignition_calls; return IGNITION_OUTPUT_OK; }
static uint32_t HAL_RCC_GetPCLK1Freq(void) { return 48000000u; }
static HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *handle)
{
    C(handle == &timer && handle->Instance == TIM5);
    return HAL_OK;
}
static HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *handle)
{
    C(handle == &timer);
    return HAL_OK;
}
static HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *handle)
{
    C(handle == &vref_adc && handle->Instance == ADC1);
    return HAL_OK;
}
static HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *handle, ADC_ChannelConfTypeDef *config)
{
    C(handle == &vref_adc && config->Channel == ADC_CHANNEL_VREFINT && config->Rank == 1u &&
      config->SamplingTime == ADC_SAMPLETIME_480CYCLES);
    return HAL_OK;
}
static void dma(DMA_HandleTypeDef *handle, DMA_Stream_TypeDef *instance, uint32_t channel,
                uint32_t direction, bool sd_dma)
{
    C(channel == DMA_CHANNEL_3 && !sd_dma);
    C((handle == &ads_rx && instance == DMA2_Stream0 && direction == DMA_PERIPH_TO_MEMORY) ||
      (handle == &ads_tx && instance == DMA2_Stream5 && direction == DMA_MEMORY_TO_PERIPH));
    handle->Instance = instance;
}
void HAL_SPI_MspInit(SPI_HandleTypeDef *handle);
static HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *handle)
{
    HAL_SPI_MspInit(handle);
    return HAL_OK;
}
static bool idle(void *ctx) { (void)ctx; return idle_allowed; }
static uint64_t st_reference_time_us(void *ctx)
{
    (void)ctx;
    uint64_t now = now_us;
    now_us += time_step;
    return now;
}
static Ads1220Result Capture_Quiesce(void *owner)
{
    C(owner == &capture);
    ++quiesce_calls;
    if (busy_remaining)
    {
        --busy_remaining;
        return ADS1220_BUSY;
    }
    return quiesce_result;
}
static Ads1220Result Capture_CheckTimeout(void *owner, uint64_t now)
{
    C(owner == &capture && now <= now_us);
    ++timeout_calls;
    return timeout_result;
}
static Ads1220Result Capture_ResumeAfterIdle(void *owner, uint64_t now)
{
    C(owner == &capture && now <= now_us && primask == 1u);
    ++resume_calls;
    return resume_result;
}
static void IgnitionServer_Service(void *owner) { C(owner == &ignition); }
static void st_logger_source_error_isr(void *owner) { C(owner == &logger); ++source_errors; }
static void st_cal_input_error_isr(void *input) { C(input == &calibration.endpoint.input); ++cal_errors; }
static st_adc_sample st_adc_decode(const uint8_t *bytes) { return (st_adc_sample){bytes[0]}; }
static bool st_ref_rate_ok(uint32_t edges, uint64_t elapsed) { return edges > 0u && elapsed > 0u; }
static void st_logger_stop(void *owner) { C(owner == &logger); ++stop_calls; }
static void st_logger_service(void *owner, uint64_t now) { C(owner == &logger && now <= now_us); ++service_calls; }
static void voltage_step(uint32_t ms, uint64_t us) { C(ms == (uint32_t)(us / 1000u)); }
static void st_debug_service(void *owner) { C(owner == &debug); }
static void st_f4_capture_drdy(void *owner, uint64_t now) { C(owner == &capture && now <= now_us); ++drdy_calls; }
static void st_radio_irq_isr(void *owner) { C(owner == &radio); ++radio_calls; }
void HAL_GPIO_EXTI_Callback(uint16_t pin);
static void HAL_GPIO_EXTI_IRQHandler(uint16_t pin)
{
    C(pin == GPIO_PIN_15 || pin == GPIO_PIN_7);
    ++exti_calls;
    HAL_GPIO_EXTI_Callback(pin);
}

#include "acquisition_board_functions.inc"

static void BoardModel_ResetService(void)
{
    idle_allowed = true;
    capture_owner = app_ready = true;
    source_fault = clock_fault = nv_capture_paused = servicing = false;
    rate_ok = rate_checked = true;
    last_valid_us = 99;
    seen_version = rate_sequence = 0;
    latest_version = 7;
    latest.bytes[0] = 0;
    latest.time_us = 99;
    capture.drdy_edges = 42;
    logger.state = ST_IDLE;
    rate_time_us = now_us = 100;
    time_step = 0;
    primask = cleared_pin = 0;
    clear_order = enable_order = 0;
    quiesce_result = timeout_result = resume_result = ADS1220_OK;
    quiesce_calls = busy_remaining = timeout_calls = waits = resume_calls = 0;
    stop_calls = source_errors = cal_errors = service_calls = 0;
    memset(irq_disables, 0, sizeof irq_disables);
    memset(irq_enables, 0, sizeof irq_enables);
}
static void BoardModel_CheckGpio(void)
{
    board_init();
    C(clocks == 127u && ignition_calls == 1u && GPIOD->id == 3u);
    C(pin_state[1][6].init_count == 1u && pin_state[1][6].mode == GPIO_MODE_OUTPUT_PP &&
      pin_state[1][6].value == GPIO_PIN_SET && pin_state[1][6].write_order < pin_state[1][6].init_order);
    for (unsigned i = 3; i <= 5; ++i)
        C(pin_state[1][i].init_count == 1u && pin_state[1][i].mode == GPIO_MODE_AF_PP &&
          pin_state[1][i].alternate == GPIO_AF5_SPI1 && pin_state[1][i].pull == GPIO_NOPULL);
    for (unsigned i = 4; i <= 7; ++i)
        C(pin_state[0][i].init_count == 1u && pin_state[0][i].mode == GPIO_MODE_OUTPUT_PP &&
          pin_state[0][i].value == GPIO_PIN_RESET && pin_state[0][i].write_order < pin_state[0][i].init_order &&
          leds.port[i - 4u] == GPIOA && leds.pin[i - 4u] == (1u << i));
    C(leds.active_high && leds.verified);
    C(pin_state[0][9].init_count == 1u && pin_state[0][9].mode == GPIO_MODE_INPUT && pin_state[0][9].pull == GPIO_PULLUP);
    C(pin_state[0][15].init_count == 1u && pin_state[0][15].mode == GPIO_MODE_IT_FALLING && pin_state[0][15].pull == GPIO_NOPULL);
    C(pin_state[2][4].init_count == 0u && pin_state[1][7].init_count == 0u && pin_state[1][8].init_count == 0u && pin_state[1][9].init_count == 0u);
    C(pin_state[2][7].mode == GPIO_MODE_IT_RISING && irq_enables[EXTI9_5_IRQn] == 1u && priorities[EXTI9_5_IRQn] == 4u);
    C(irq_enables[EXTI15_10_IRQn] == 0u); /* Ownership starts after ADC configuration. */
    C(ads_spi.hdmarx == &ads_rx && ads_spi.hdmatx == &ads_tx &&
      irq_enables[DMA2_Stream0_IRQn] == 1u && irq_enables[DMA2_Stream5_IRQn] == 1u &&
      irq_enables[SPI1_IRQn] == 1u && priorities[SPI1_IRQn] == 0u);
    C(ads_spi.Init.CLKPhase == SPI_PHASE_2EDGE && ads_spi.Init.BaudRatePrescaler == SPI_BAUDRATEPRESCALER_32);
    C(rf_spi.Init.CLKPhase == SPI_PHASE_1EDGE && rf_spi.Init.BaudRatePrescaler == SPI_BAUDRATEPRESCALER_16);
    C(timer.Init.Prescaler == 95u && timer.Init.Period == UINT32_MAX);
    card_level = GPIO_PIN_SET;
    C(!present(NULL));
    card_level = GPIO_PIN_RESET;
    C(present(NULL));
    capture_owner = false;
    EXTI15_10_IRQHandler();
    C(drdy_calls == 0u && radio_calls == 0u);
    capture_owner = true;
    EXTI15_10_IRQHandler();
    C(drdy_calls == 1u && radio_calls == 0u);
    HAL_GPIO_EXTI_Callback(GPIO_PIN_4);
    C(drdy_calls == 1u && radio_calls == 0u);
    EXTI9_5_IRQHandler();
    C(exti_calls == 3u && drdy_calls == 1u && radio_calls == 1u);
}
static void BoardModel_CheckPauseResume(void)
{
    BoardModel_ResetService();
    idle_allowed = false;
    C(!Acquisition_NvIdle(NULL) && !nv_capture_paused && !quiesce_calls);
    idle_allowed = true;
    capture_owner = false;
    C(Acquisition_NvIdle(NULL) && !quiesce_calls && !nv_capture_paused);
    capture_owner = true;
    source_fault = true;
    C(Acquisition_NvIdle(NULL) && !quiesce_calls);
    source_fault = false;
    busy_remaining = 2;
    time_step = 100;
    C(Acquisition_NvIdle(NULL));
    C(nv_capture_paused && quiesce_calls == 3u && waits == 2u && timeout_calls == 2u);
    C(!rate_ok && !rate_checked && last_valid_us == 0u && irq_disables[EXTI15_10_IRQn] == 1u);
    C(Acquisition_NvIdle(NULL) && quiesce_calls == 3u && irq_disables[EXTI15_10_IRQn] == 1u);
    time_step = 0;
    now_us = 10000;
    primask = 1;
    fast_service();
    C(!nv_capture_paused && resume_calls == 1u && !source_fault && primask == 1u);
    C(cleared_pin == GPIO_PIN_15 && irq_enables[EXTI15_10_IRQn] == 1u && clear_order < enable_order);
    C(rate_time_us == now_us && rate_sequence == capture.drdy_edges && seen_version == latest_version);
    C(!rate_ok && !rate_checked && last_valid_us == 0u && !stop_calls && service_calls == 1u);
    fast_service();
    C(resume_calls == 1u && irq_enables[EXTI15_10_IRQn] == 1u && primask == 1u);
    BoardModel_ResetService();
    busy_remaining = 20;
    time_step = 500;
    C(!Acquisition_NvIdle(NULL) && !nv_capture_paused && irq_disables[EXTI15_10_IRQn] == 0u);
    C(quiesce_calls == 2u && timeout_calls == 2u && waits == 1u);
    BoardModel_ResetService();
    busy_remaining = 1;
    timeout_result = ADS1220_ERROR;
    C(!Acquisition_NvIdle(NULL) && !nv_capture_paused && !waits);
    BoardModel_ResetService();
    C(Acquisition_NvIdle(NULL));
    resume_result = ADS1220_ERROR;
    fast_service();
    C(source_fault && !nv_capture_paused && resume_calls == 1u && !irq_enables[EXTI15_10_IRQn] && primask == 0u);
    BoardModel_ResetService();
    C(Acquisition_NvIdle(NULL));
    clock_fault = source_fault = true; /* CSS may arrive while Flash is being programmed. */
    fast_service();
    C(source_fault && !irq_enables[EXTI15_10_IRQn] && cleared_pin == GPIO_PIN_15);
    BoardModel_ResetService();
    logger.state = ST_CAPTURING;
    timeout_result = ADS1220_ERROR;
    fast_service();
    C(source_fault && source_errors == 1u && cal_errors == 1u && stop_calls == 1u && quiesce_calls == 1u);
}
int main(void)
{
    BoardModel_CheckGpio();
    BoardModel_CheckPauseResume();
    printf("PASS actual acquisition board GPIO/SPI/EXTI/NV functions: %u assertions; PB3/4/5 SPI1, PB6 CS, PA4..7 LEDs, PA9 card detect, PA15 DRDY, pause/resume and clock-fault IRQ fence. Host interface model only.\n", checks);
    return 0;
}
