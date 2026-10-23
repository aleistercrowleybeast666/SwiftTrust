/* Executable REFERENCE board. See Models/REFERENCE_APPLICATION.md before use.
 * R8 output is disabled pending review; PA0 defaults low. PA11/PA12 untouched. */
#if defined(ST_REFERENCE_APPLICATION)
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_adc.h"
#include "st_reference.h"
#include "st_console.h"
#include "st_flash_journal.h"
#include "st_ads_hal.h"
#include "st_f4_capture.h"
#include "st_sdio_hal.h"
#include "st_diskio.h"
#include "st_cal_system.h"
#include "st_radio_hal.h"
#include "st_link_runtime.h"
#include "st_debug_uart_hal.h"
#include "st_ignition_f4.h"
#include "st_battery.h"
#include <stdio.h>
#include <string.h>

static SPI_HandleTypeDef ads_spi, rf_spi;
static DMA_HandleTypeDef ads_rx, ads_tx, sd_rx, sd_tx;
static SD_HandleTypeDef sd;
static ADC_HandleTypeDef vref_adc;
static TIM_HandleTypeDef timer;
static st_ads_hal ads_hal;
static st_ads ads;
static st_f4_capture_port capture;
static st_sdio_hal sd_hal;
static st_block_device block;
static FATFS volume;
static st_fatfs_worker worker;
static st_logger logger;
static st_cal_system calibration;
static st2_cal_port cal_port;
static st_ref_sensor sensor;
static st_radio_hal radio_hal;
static st_radio radio;
static st_link_runtime link;
static st2_server server;
static IgnitionServer ignition;
static st_debug_uart_hal uart;
static st_debug debug;
static st_journal journal;
static st_pairing pairing;
static AutoLink auto_link;
static st_console console;
static st_flash_window flash_window;
static volatile uint32_t time_high;
static volatile uint16_t vref_raw;
static volatile bool vref_done;
static volatile st_capture latest;
static volatile uint32_t latest_version;
static uint32_t seen_version, rate_sequence;
static uint64_t last_valid_us, rate_time_us;
static st_voltage supply;
static st_voltage_view supply_view;
static BatteryMonitor battery;
static st_voltage_view battery_view;
static const st_voltage_policy supply_policy = {ST_VDDA_DISPLAY_LEVELS_MV, ST_VDDA_HYSTERESIS_MV,
                                                ST_VDDA_FRESH_MS, true};
static const st_f4_led_port leds = {
    {GPIOB, GPIOB, GPIOB, GPIOB}, {GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_8, GPIO_PIN_9}, true, true};
static volatile bool capture_owner;
static bool mounted, rate_ok, rate_checked, paired, change_pair, servicing, app_ready, adc_busy;
static bool source_fault;
static uint32_t adc_started_ms, last_adc_ms;
static bool adc_battery_phase, adc_next_battery;
static void fast_service(void);

uint64_t st_reference_time_us(void *ctx)
{
    (void)ctx;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t high = time_high, low = TIM5->CNT;
    if (TIM5->SR & TIM_SR_UIF)
    {
        high++;
        low = TIM5->CNT;
    }
    __set_PRIMASK(mask);
    return ((uint64_t)high << 32) | low;
}
static uint32_t now_ms(void *ctx)
{
    return (uint32_t)(st_reference_time_us(ctx) / 1000);
}
static void emit(void *ctx, const char *s)
{
    (void)ctx;
    (void)st_debug_write(&debug, (const uint8_t *)s, strlen(s), now_ms(NULL));
}
static void fatal(void)
{
    (void)IgnitionPulse_Stop(NULL);
    HAL_NVIC_DisableIRQ(EXTI4_IRQn);
    atomic_store(&logger.capture_enabled, false);
    if (uart.verified)
        emit(NULL, "FATAL INIT; RESET / CHECK REFERENCE WIRING\r\n");
    for (;;)
    {
        if (uart.verified)
            st_debug_service(&debug);
        __WFI();
    }
}
static void need(HAL_StatusTypeDef result)
{
    if (result != HAL_OK)
        fatal();
}
static void gpio(GPIO_TypeDef *port, uint32_t pins, uint32_t mode, uint32_t pull, uint32_t af)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pins;
    g.Mode = mode;
    g.Pull = pull;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = af;
    HAL_GPIO_Init(port, &g);
}
static void irq(IRQn_Type n, unsigned priority)
{
    HAL_NVIC_SetPriority(n, priority, 0);
    HAL_NVIC_EnableIRQ(n);
}
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef o = {0};
    RCC_ClkInitTypeDef c = {0};
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
    /* 8 MHz HSE crystal; preserve 84 MHz SYSCLK and the 48 MHz SDIO domain. */
    o.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    o.HSEState = RCC_HSE_ON;
    o.PLL.PLLState = RCC_PLL_ON;
    o.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    o.PLL.PLLM = 8;
    o.PLL.PLLN = 336;
    o.PLL.PLLP = RCC_PLLP_DIV4;
    o.PLL.PLLQ = 7;
    need(HAL_RCC_OscConfig(&o));
    c.ClockType =
        RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    c.AHBCLKDivider = RCC_SYSCLK_DIV1;
    c.APB1CLKDivider = RCC_HCLK_DIV2;
    c.APB2CLKDivider = RCC_HCLK_DIV1;
    need(HAL_RCC_ClockConfig(&c, FLASH_LATENCY_2));
}
static void dma(DMA_HandleTypeDef *d, DMA_Stream_TypeDef *instance, uint32_t channel,
                uint32_t direction, bool sd_dma)
{
    d->Instance = instance;
    d->Init.Channel = channel;
    d->Init.Direction = direction;
    d->Init.PeriphInc = DMA_PINC_DISABLE;
    d->Init.MemInc = DMA_MINC_ENABLE;
    d->Init.PeriphDataAlignment = sd_dma ? DMA_PDATAALIGN_WORD : DMA_PDATAALIGN_BYTE;
    d->Init.MemDataAlignment = sd_dma ? DMA_MDATAALIGN_WORD : DMA_MDATAALIGN_BYTE;
    d->Init.Mode = DMA_NORMAL;
    d->Init.Priority = sd_dma ? DMA_PRIORITY_LOW : DMA_PRIORITY_VERY_HIGH;
    d->Init.FIFOMode = sd_dma ? DMA_FIFOMODE_ENABLE : DMA_FIFOMODE_DISABLE;
    d->Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    d->Init.MemBurst = sd_dma ? DMA_MBURST_INC4 : DMA_MBURST_SINGLE;
    d->Init.PeriphBurst = sd_dma ? DMA_PBURST_INC4 : DMA_PBURST_SINGLE;
    need(HAL_DMA_Init(d));
}
void HAL_SPI_MspInit(SPI_HandleTypeDef *h)
{
    if (h->Instance == SPI1)
    {
        __HAL_RCC_SPI1_CLK_ENABLE();
        __HAL_RCC_DMA2_CLK_ENABLE();
        gpio(GPIOA, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_MODE_AF_PP, GPIO_NOPULL,
             GPIO_AF5_SPI1);
        dma(&ads_rx, DMA2_Stream0, DMA_CHANNEL_3, DMA_PERIPH_TO_MEMORY, false);
        dma(&ads_tx, DMA2_Stream5, DMA_CHANNEL_3, DMA_MEMORY_TO_PERIPH, false);
        __HAL_LINKDMA(h, hdmarx, ads_rx);
        __HAL_LINKDMA(h, hdmatx, ads_tx);
        irq(DMA2_Stream0_IRQn, 0);
        irq(DMA2_Stream5_IRQn, 0);
        irq(SPI1_IRQn, 0);
    }
    else if (h->Instance == SPI2)
    {
        __HAL_RCC_SPI2_CLK_ENABLE();
        gpio(GPIOB, GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_MODE_AF_PP, GPIO_NOPULL,
             GPIO_AF5_SPI2);
    }
}
void HAL_SD_MspInit(SD_HandleTypeDef *h)
{
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    gpio(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11, GPIO_MODE_AF_PP,
         GPIO_PULLUP, GPIO_AF12_SDIO);
    gpio(GPIOC, GPIO_PIN_12, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_AF12_SDIO);
    gpio(GPIOD, GPIO_PIN_2, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_AF12_SDIO);
    dma(&sd_rx, DMA2_Stream3, DMA_CHANNEL_4, DMA_PERIPH_TO_MEMORY, true);
    dma(&sd_tx, DMA2_Stream6, DMA_CHANNEL_4, DMA_MEMORY_TO_PERIPH, true);
    __HAL_LINKDMA(h, hdmarx, sd_rx);
    __HAL_LINKDMA(h, hdmatx, sd_tx);
    irq(SDIO_IRQn, 5);
    irq(DMA2_Stream3_IRQn, 6);
    irq(DMA2_Stream6_IRQn, 6);
}
void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    if (h->Instance == ADC1)
    {
        __HAL_RCC_ADC1_CLK_ENABLE();
        gpio(GPIOC, GPIO_PIN_0, GPIO_MODE_ANALOG, GPIO_NOPULL, 0);
        irq(ADC_IRQn, 7);
    }
}
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *h)
{
    if (h->Instance == TIM5)
    {
        __HAL_RCC_TIM5_CLK_ENABLE();
        irq(TIM5_IRQn, 3);
    }
}
static void spi_init(SPI_HandleTypeDef *h, SPI_TypeDef *instance, bool mode1)
{
    h->Instance = instance;
    h->Init.Mode = SPI_MODE_MASTER;
    h->Init.Direction = SPI_DIRECTION_2LINES;
    h->Init.DataSize = SPI_DATASIZE_8BIT;
    h->Init.CLKPolarity = SPI_POLARITY_LOW;
    h->Init.CLKPhase = mode1 ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    h->Init.NSS = SPI_NSS_SOFT;
    h->Init.BaudRatePrescaler = mode1 ? SPI_BAUDRATEPRESCALER_32 : SPI_BAUDRATEPRESCALER_16;
    h->Init.FirstBit = SPI_FIRSTBIT_MSB;
    h->Init.TIMode = SPI_TIMODE_DISABLE;
    h->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    need(HAL_SPI_Init(h));
}
static void board_init(void)
{
    if (IgnitionPulse_Init() != IGNITION_OUTPUT_OK)
        fatal();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    gpio(GPIOA, GPIO_PIN_4, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12 | GPIO_PIN_2, GPIO_PIN_SET);
    gpio(GPIOB, GPIO_PIN_12 | GPIO_PIN_2, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET);
    gpio(GPIOC, GPIO_PIN_5, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);
    gpio(GPIOB, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,
         0);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    gpio(GPIOB, GPIO_PIN_0, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    gpio(GPIOC, GPIO_PIN_6, GPIO_MODE_INPUT, GPIO_NOPULL, 0);
    gpio(GPIOB, GPIO_PIN_5, GPIO_MODE_INPUT, GPIO_PULLUP, 0);
    gpio(GPIOC, GPIO_PIN_4, GPIO_MODE_IT_FALLING, GPIO_NOPULL, 0);
    gpio(GPIOC, GPIO_PIN_7, GPIO_MODE_IT_RISING, GPIO_NOPULL, 0);
    irq(EXTI9_5_IRQn, 4);
    timer.Instance = TIM5;
    timer.Init.Prescaler = 83;
    timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    timer.Init.Period = UINT32_MAX;
    timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    need(HAL_TIM_Base_Init(&timer));
    need(HAL_TIM_Base_Start_IT(&timer));
    spi_init(&ads_spi, SPI1, true);
    spi_init(&rf_spi, SPI2, false);
    vref_adc.Instance = ADC1;
    vref_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    vref_adc.Init.Resolution = ADC_RESOLUTION_12B;
    vref_adc.Init.ScanConvMode = DISABLE;
    vref_adc.Init.ContinuousConvMode = DISABLE;
    vref_adc.Init.DiscontinuousConvMode = DISABLE;
    vref_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    vref_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    vref_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    vref_adc.Init.NbrOfConversion = 1;
    vref_adc.Init.DMAContinuousRequests = DISABLE;
    vref_adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    need(HAL_ADC_Init(&vref_adc));
    ADC_ChannelConfTypeDef channel = {0};
    channel.Channel = ADC_CHANNEL_VREFINT;
    channel.Rank = 1;
    channel.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    need(HAL_ADC_ConfigChannel(&vref_adc, &channel));
}
static bool present(void *ctx)
{
    (void)ctx;
    return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_RESET;
}
static void yield(void *ctx)
{
    (void)ctx;
    fast_service();
    __WFI();
}
static bool idle(void *ctx)
{
    (void)ctx;
    return supply.valid && supply.mv >= ST_VDDA_MIN_MV && supply.mv <= ST_VDDA_MAX_MV &&
           supply_view.age_ms <= ST_VDDA_FRESH_MS && !IgnitionServer_IsBusy(&ignition) &&
           !IgnitionPulse_IsRunning(NULL) && logger.state == ST_IDLE && !logger.inflight &&
           !atomic_load(&logger.capture_enabled) &&
           (!app_ready || !mounted || st_fatfs_idle(&worker)) &&
           (!app_ready || !cal_port.locked || !cal_port.locked(cal_port.ctx));
}
static void changed(void *ctx)
{
    (void)ctx;
    change_pair = true;
}
static void status(void *ctx)
{
    (void)ctx;
    char s[160];
    (void)snprintf(s, sizeof s, "ACQ %u TF %u ADC %u RATE %u RF %u NV %u DROP %lu ERR %lu\r\n",
                   (unsigned)logger.state, mounted ? 1u : 0u, ads.ready ? 1u : 0u,
                   rate_ok ? 1u : 0u, (!radio.failed && radio.phase >= 13) ? 1u : 0u,
                   (unsigned)pairing.store.status, (unsigned long)atomic_load(&logger.ring.dropped),
                   (unsigned long)st_logger_error_count(&logger));
    emit(NULL, s);
    (void)snprintf(s, sizeof s, "BAT PC0 %lu mV VALID %u / VDDA %lu mV\r\n",
                   (unsigned long)battery.voltage.mv, battery.voltage.valid ? 1u : 0u,
                   (unsigned long)supply.mv);
    emit(NULL, s);
    (void)snprintf(s, sizeof s, "IGN STATE %u / MCU SIGNAL %s / NO EXTERNAL FEEDBACK\r\n",
                   (unsigned)ignition.state, IgnitionPulse_IsRunning(NULL) ? "PULSE" : "LOW");
    emit(NULL, s);
}
static IgnitionKeyResult Ignition_SetConsoleKey(void *ctx, const uint8_t key[16])
{
    (void)ctx;
    if (!paired || !idle(NULL) || radio.tx_active || radio.tx_pending || link.length)
        return IGNITION_KEY_BUSY;
    server.active = server.offered = false;
    (void)IgnitionServer_SetSession(&ignition, 0, false);
    return IgnitionServer_SetKey(&ignition, key);
}
static void voltage_step(uint32_t ms, uint64_t us)
{
    if (vref_done)
    {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        uint16_t raw = vref_raw;
        vref_done = false;
        __set_PRIMASK(mask);
        (void)HAL_ADC_Stop_IT(&vref_adc);
        adc_busy = false;
        if (adc_battery_phase)
            Battery_Update(&battery, raw, &supply, us);
        else
        {
            uint32_t mv = 0;
            bool ok = st_ref_vdda_mv(*VREFINT_CAL_ADDR, VREFINT_CAL_VREF, raw, &mv);
            supply = (st_voltage){mv, us, true, ok};
            adc_next_battery = ok && ST_ACQ_BATTERY_PRESENT && ST_ACQ_BATTERY_VERIFIED;
            if (!ok)
                Battery_Invalidate(&battery);
        }
    }
    if (adc_busy && (uint32_t)(ms - adc_started_ms) > 20)
    {
        (void)HAL_ADC_Stop_IT(&vref_adc);
        adc_busy = false;
        if (!adc_battery_phase)
            supply.valid = false;
        Battery_Invalidate(&battery);
        adc_next_battery = false;
    }
    if (!adc_busy && (adc_next_battery || (uint32_t)(ms - last_adc_ms) >= 100))
    {
        adc_battery_phase = adc_next_battery;
        adc_next_battery = false;
        if (!adc_battery_phase)
            last_adc_ms = ms;
        adc_started_ms = ms;
        ADC_ChannelConfTypeDef channel = {.Channel = adc_battery_phase ? ADC_CHANNEL_10
                                                                       : ADC_CHANNEL_VREFINT,
                                          .Rank = 1,
                                          .SamplingTime = ADC_SAMPLETIME_480CYCLES};
        adc_busy = HAL_ADC_ConfigChannel(&vref_adc, &channel) == HAL_OK &&
                   HAL_ADC_Start_IT(&vref_adc) == HAL_OK;
        if (!adc_busy)
        {
            if (!adc_battery_phase)
                supply.valid = false;
            Battery_Invalidate(&battery);
        }
    }
    supply_view = st_voltage_evaluate(&supply_policy, &supply, us, supply_view);
    battery_view = Battery_Evaluate(&battery, us);
    (void)st_f4_voltage_leds(&leds, battery_view);
}
static void fast_service(void)
{
    if (!app_ready || servicing)
        return;
    servicing = true;
    IgnitionServer_Service(&ignition);
    uint64_t us = st_reference_time_us(NULL);
    uint32_t ms = (uint32_t)(us / 1000);
    if (capture_owner && seen_version != latest_version)
    {
        st_capture c;
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        c = latest;
        seen_version = latest_version;
        __set_PRIMASK(mask);
        st_adc_sample decoded = st_adc_decode(c.bytes);
        if (!decoded.flags)
            last_valid_us = c.time_us;
    }
    if (capture_owner && us - rate_time_us >= ST_ADC_RATE_WINDOW_US)
    {
        uint32_t sequence = capture.sequence;
        rate_ok = st_ref_rate_ok(sequence - rate_sequence, us - rate_time_us);
        rate_checked = true;
        rate_sequence = sequence;
        rate_time_us = us;
    }
    bool fresh = last_valid_us && us >= last_valid_us && us - last_valid_us <= ST_ADC_FRESH_US;
    if ((logger.state == ST_STARTING || logger.state == ST_CAPTURING) && (!fresh || !rate_ok))
    {
        if (!source_fault)
        {
            st_logger_source_error_isr(&logger);
            source_fault = true;
        }
        (void)st_logger_stop(&logger);
    }
    /* RESET/config or an idle Flash stall can leave DRDY already low, so there
     * may be no new falling edge. Drain it only while idle, outside a wizard.
     * Such a timestamp is never offered to a recording or calibration sample. */
    if (capture_owner && logger.state == ST_IDLE && !atomic_load(&logger.capture_enabled) &&
        (!cal_port.locked || !cal_port.locked(cal_port.ctx)) &&
        calibration.endpoint.zero.state != ST_ZERO_COLLECTING)
    {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (!atomic_load(&capture.busy) && HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_RESET)
            st_f4_capture_drdy(&capture, us);
        __set_PRIMASK(mask);
    }
    st_logger_service(&logger, us);
    voltage_step(ms, us);
    st_debug_service(&debug);
    servicing = false;
}
static bool cal_request(void *ctx, const uint8_t p[14])
{
    (void)ctx;
    return (sensor.configured || p[0] >= 7) && cal_port.request &&
           cal_port.request(cal_port.ctx, p);
}
static bool cal_snapshot(void *ctx, uint8_t p[64])
{
    (void)ctx;
    return cal_port.snapshot && cal_port.snapshot(cal_port.ctx, p);
}
static bool cal_locked(void *ctx)
{
    (void)ctx;
    return cal_port.locked && cal_port.locked(cal_port.ctx);
}
static void cal_reset(void *ctx)
{
    (void)ctx;
    if (cal_port.reset)
        cal_port.reset(cal_port.ctx);
}
static bool Zero_IsWaiting(void *ctx)
{
    (void)ctx;
    return cal_port.start_waiting && cal_port.start_waiting(cal_port.ctx);
}
static bool Zero_HasFailed(void *ctx)
{
    (void)ctx;
    return cal_port.start_failed && cal_port.start_failed(cal_port.ctx);
}
static bool Zero_ReadSnapshot(void *ctx, uint8_t view[12])
{
    (void)ctx;
    return cal_port.sensor_snapshot && cal_port.sensor_snapshot(cal_port.ctx, view);
}
static void Zero_BeginStart(void *ctx)
{
    (void)ctx;
    if (cal_port.begin_start)
        cal_port.begin_start(cal_port.ctx);
}
static bool prepare(void *ctx)
{
    (void)ctx;
    uint64_t us = st_reference_time_us(NULL);
    if (!mounted || block.fault || !present(NULL) || !ads.ready || !rate_ok || !last_valid_us ||
        us - last_valid_us > ST_ADC_FRESH_US || !st_fatfs_idle(&worker) || !idle(NULL))
        return false;
    if (!st_ref_prepare_start(&logger, cal_port, server.epoch, true, sensor.configured))
        return false;
    source_fault = false;
    return true;
}
static bool reserve_epoch(void *ctx, uint64_t counter, uint32_t *epoch)
{
    (void)ctx;
    if (logger.state == ST_STARTING || logger.state == ST_CAPTURING || logger.state == ST_STOPPING)
    {
        return false; /* Never stop recording to make an NV write window. */
    }
    if (!idle(NULL))
        return false;
    st2_nv_port nv = st_nv_link_port(&pairing.store);
    bool ok = nv.reserve_epoch(nv.ctx, counter, epoch);
    rate_ok = false;
    rate_checked = false;
    rate_time_us = st_reference_time_us(NULL);
    rate_sequence = capture.sequence;
    return ok;
}
static bool Ignition_CheckLocal(void *ctx, uint8_t mode)
{
    (void)ctx;
    bool voltage_ok = supply.valid && supply.mv >= ST_VDDA_MIN_MV && supply.mv <= ST_VDDA_MAX_MV &&
                      supply_view.age_ms <= ST_VDDA_FRESH_MS;
    if (!voltage_ok || radio.failed || cal_locked(NULL))
        return false;
    if (mode == 1)
        return logger.state == ST_IDLE;
    return mode == 2 && logger.state == ST_CAPTURING && !source_fault && !ads.failed && rate_ok &&
           last_valid_us && st_reference_time_us(NULL) - last_valid_us <= ST_ADC_FRESH_US &&
           mounted && present(NULL) && !block.fault && !st_logger_error_count(&logger);
}
static void server_init(void)
{
    (void)IgnitionPulse_Stop(NULL);
    paired = st_nv_server_init(&server, &pairing.store, &logger, st_ref_link_timing());
    IgnitionServer_Init(&ignition, (const uint8_t *)UID_BASE, pairing.store.identity.peer_uid);
    if (ST_OUTPUT_HARDWARE_ENABLED &&
        IgnitionServer_BindOutput(
            &ignition, (IgnitionOutputPort){NULL, now_ms, Ignition_CheckLocal, IgnitionPulse_Start,
                                            IgnitionPulse_Stop, IgnitionPulse_IsRunning}) !=
            IGNITION_OUTPUT_OK)
        fatal();
    if (paired && IgnitionServer_SetKey(&ignition, st_ref_ignition_key()) != IGNITION_KEY_OK)
        fatal();
    server.ignition = &ignition;
    if (paired)
        server.nv = (st2_nv_port){NULL, reserve_epoch, NULL};
    if (paired)
        st2_server_attach_calibration(&server, (st2_cal_port){.ctx = NULL,
                                                              .request = cal_request,
                                                              .snapshot = cal_snapshot,
                                                              .locked = cal_locked,
                                                              .prepare_start = prepare,
                                                              .reset = cal_reset,
                                                              .start_waiting = Zero_IsWaiting,
                                                              .start_failed = Zero_HasFailed,
                                                              .sensor_snapshot = Zero_ReadSnapshot,
                                                              .begin_start = Zero_BeginStart});
}
static void storage_init(void)
{
    st_fatfs_init(&worker, false);
    if (!present(NULL))
        return;
    sd.Instance = SDIO;
    sd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    sd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    sd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    sd.Init.BusWide = SDIO_BUS_WIDE_1B;
    sd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    sd.Init.ClockDiv = 2;
    if (HAL_SD_Init(&sd) != HAL_OK ||
        HAL_SD_ConfigWideBusOperation(&sd, SDIO_BUS_WIDE_4B) != HAL_OK)
        return;
    if (!st_sdio_hal_bind(&sd_hal, &sd, NULL, present, now_ms, yield, true))
        return;
    st_block_bind(&block, st_sdio_hal_port(&sd_hal), sd.SdCard.LogBlockNbr, 1, 250, true);
    st_diskio_bind(&block);
    mounted = f_mount(&volume, "", 1) == FR_OK;
    st_fatfs_init(&worker, mounted);
}
static void sensor_init(void)
{
    if (mounted)
    {
        FIL file;
        UINT count = 0;
        char text[1024];
        if (f_open(&file, "SENSOR.CFG", FA_READ) == FR_OK)
        {
            bool size = f_size(&file) > 0 && f_size(&file) <= sizeof text;
            FRESULT read = size ? f_read(&file, text, sizeof text, &count) : FR_INVALID_PARAMETER;
            FRESULT close = f_close(&file);
            if (size && read == FR_OK && close == FR_OK)
                (void)st_ref_sensor_parse(&sensor, text, count);
        }
    }
    (void)st_cal_system_init(&calibration, &logger, &worker, NULL, sensor.binding, sensor.policy,
                             NULL, st_reference_time_us);
    calibration.endpoint.auto_zero =
        sensor.configured ? sensor.auto_zero : ST_AUTO_ZERO_DEFAULT != 0;
    cal_port = st_cal_endpoint_port(&calibration.endpoint);
}
void _init(void)
{
}
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    board_init();
    if (!st_debug_uart_configure(&uart, true))
        fatal();
    st_debug_init(&debug, st_debug_uart_port(&uart));
    emit(NULL, "SwiftThrust ACQ REFERENCE; INFO / STATUS\r\n");
    st_log_config cfg = {.adc_clock_hz = ST_REF_ADC_HZ,
                         .osr = ST_REF_ADC_OSR,
                         .rate_millihz = ST_REF_ADC_RATE_MILLIHZ,
                         .gain_ch0 = 1,
                         .gain_ch1 = 1};
    st_logger_init(&logger, cfg, st_fatfs_port(&worker));
    flash_window = (st_flash_window){NULL, idle};
    (void)st_journal_open(&journal, st_flash_journal_port(&flash_window));
    (void)st_pairing_open(&pairing, st_journal_storage(&journal), (const uint8_t *)UID_BASE,
                          ST_NV_ACQUISITION);
    AutoLink_Init(&auto_link, &pairing, st_ref_ignition_key(), ST_PROJECT_PAIR_ID, NULL, idle);
    console = (st_console){.pair = &pairing,
                           .emit = emit,
                           .idle = idle,
                           .changed = changed,
                           .status = status,
                           .ignition_key = Ignition_SetConsoleKey};
    storage_init();
    sensor_init();
    server_init();
    if (!st_ads_hal_bind(&ads_hal, &ads_spi, GPIOA, GPIO_PIN_4, GPIOC, GPIO_PIN_5, HAL_GetTick, 20,
                         true))
        fatal();
    st_ads_init(&ads, st_ads_hal_port(&ads_hal), st_ref_ads_config(true),
                (uint32_t)st_reference_time_us(NULL));
    radio_hal = (st_radio_hal){.spi = &rf_spi,
                               .cs = GPIOB,
                               .cs_pin = GPIO_PIN_12,
                               .busy = GPIOC,
                               .busy_pin = GPIO_PIN_6,
                               .reset = GPIOB,
                               .reset_pin = GPIO_PIN_2,
                               .now_ms = now_ms,
                               .yield = yield,
                               .spi_timeout_ms = 5,
                               .busy_timeout_ms = 20,
                               .reset_low_ms = 2,
                               .reset_settle_ms = 20};
    if (!st_radio_hal_bind(&radio_hal, true))
        fatal();
    st_radio_init(&radio, st_radio_hal_port(&radio_hal), st_ref_radio_config(true));
    if (!st_link_runtime_init(&link, &radio))
        fatal();
    link.auto_link = &auto_link;
    app_ready = true;
    for (;;)
    {
        uint64_t us = st_reference_time_us(NULL);
        fast_service();
        if (!capture_owner && !ads.failed)
        {
            st_ads_tick(&ads, (uint32_t)us);
            if (ads.ready && st_ads_hal_idle(&ads_hal))
            {
                st_f4_capture_init(&capture, &ads_spi, GPIOA, GPIO_PIN_4, &logger, true);
                st_f4_capture_bind_calibration(&capture, &calibration.endpoint.input);
                capture_owner = true;
                rate_time_us = us;
                __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_4);
                irq(EXTI4_IRQn, 0);
            }
        }
        change_pair |= AutoLink_TakeChanged(&auto_link);
        if (change_pair)
        {
            change_pair = false;
            server_init();
            rate_ok = false;
            rate_checked = false;
            rate_time_us = us;
            rate_sequence = capture.sequence;
        }
        st_cal_endpoint_step(&calibration.endpoint);
        {
            bool fresh = last_valid_us && us - last_valid_us <= ST_ADC_FRESH_US;
            st2_measurement measurement = {
                .tf =
                    block.fault || !present(NULL)
                        ? ST2_TF_FAULT
                        : (worker.opened ? ST2_TF_OPEN : (mounted ? ST2_TF_READY : ST2_TF_UNKNOWN)),
                .voltage_fresh = battery.voltage.valid && battery_view.age_ms <= BATTERY_FRESH_MS,
                .adc_fresh = fresh,
                /* Not-yet-measured startup/reconnect readiness is UNKNOWN, not
                 * a permanent quality error. START still requires rate_ok. */
                .quality_problem = ads.failed || (rate_checked && !rate_ok) || source_fault ||
                                   st_logger_error_count(&logger) > 0,
                .mv = battery.voltage.valid && battery.voltage.mv <= 65534
                          ? (uint16_t)battery.voltage.mv
                          : UINT16_MAX};
            st_link_server_step(&link, &server, measurement, us);
        }
        /* RF faults never interrupt local ADC/TF recording. The execution policy
         * independently removes authorization/cuts the signal on a radio fault. */
        HAL_GPIO_WritePin(
            GPIOB, GPIO_PIN_0,
            Link_GetLed(Link_GetHealth(server.have_peer_status, server.peer_received_ms,
                                       now_ms(NULL), radio.failed),
                        now_ms(NULL))
                ? GPIO_PIN_SET
                : GPIO_PIN_RESET);
        /* Every sector wait yields to fast_service; neither calibration, radio nor
         * FatFs is re-entered. The ADC ISR remains independent of this volume owner. */
        st_fatfs_worker_step(&worker);
        fast_service();
        uint8_t byte;
        for (unsigned i = 0; i < 32 && st_debug_uart_read(&uart, &byte); i++)
            st_console_feed(&console, byte);
        __WFI();
    }
}
void SysTick_Handler(void)
{
    HAL_IncTick();
}
void TIM5_IRQHandler(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (TIM5->SR & TIM_SR_UIF)
    {
        TIM5->SR = ~TIM_SR_UIF;
        time_high++;
    }
    __set_PRIMASK(mask);
}
void EXTI4_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}
void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
}
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == GPIO_PIN_4 && capture_owner)
        st_f4_capture_drdy(&capture, st_reference_time_us(NULL));
    else if (pin == GPIO_PIN_7)
        st_radio_irq_isr(&radio);
}
void DMA2_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&ads_rx);
}
void DMA2_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&ads_tx);
}
void DMA2_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sd_rx);
}
void DMA2_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sd_tx);
}
void SPI1_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&ads_spi);
}
void SDIO_IRQHandler(void)
{
    HAL_SD_IRQHandler(&sd);
}
void ADC_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&vref_adc);
}
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&uart.uart);
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h)
{
    if (h != &ads_spi)
        return;
    if (capture_owner)
    {
        if (atomic_load(&capture.busy))
        {
            latest.time_us = capture.pending_time_us;
            latest.sequence = capture.pending_sequence;
            for (unsigned i = 0; i < 12; i++)
                latest.bytes[i] = capture.rx[i];
            latest_version++;
        }
        st_f4_capture_complete(&capture, h);
    }
    else
        st_ads_hal_complete(&ads_hal, h);
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h)
{
    if (capture_owner)
        st_f4_capture_error(&capture, h);
    else
        st_ads_hal_error(&ads_hal, h);
}
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *h)
{
    st_sdio_hal_rx_complete(&sd_hal, h);
}
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *h)
{
    st_sdio_hal_tx_complete(&sd_hal, h);
}
void HAL_SD_ErrorCallback(SD_HandleTypeDef *h)
{
    st_sdio_hal_error(&sd_hal, h);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
    if (h == &vref_adc)
    {
        vref_raw = (uint16_t)HAL_ADC_GetValue(h);
        vref_done = true;
    }
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *h)
{
    if (h == &vref_adc)
    {
        vref_raw = 0;
        vref_done = true;
    }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h)
{
    st_debug_uart_tx_complete(&uart, h);
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    st_debug_uart_rx_complete(&uart, h);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    st_debug_uart_error(&uart, h);
}
#endif
