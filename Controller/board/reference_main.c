/* Executable replaceable reference board; PA11/PA12 reservations untouched. */
#if defined(ST_REFERENCE_APPLICATION)
#include "stm32g0xx_hal.h"
#include "stm32g0xx_ll_adc.h"
#include "st_reference.h"
#include "st_console.h"
#include "st_flash_journal.h"
#include "st_radio_hal.h"
#include "st_link_runtime.h"
#include "st_debug_uart_hal.h"
#include "st_g0_oled.h"
#include "st_cal_remote.h"
#include <stdio.h>
#include <string.h>
static SPI_HandleTypeDef rf_spi;
static I2C_HandleTypeDef i2c;
static TIM_HandleTypeDef encoder_timer;
static ADC_HandleTypeDef vref_adc;
static st_debug_uart_hal uart;
static st_debug debug;
static st_radio_hal radio_hal;
static st_radio radio;
static st_link_runtime link;
static st2_client client;
static IgnitionClient ignition;
static st_g0_oled_port oled_port;
static st_oled oled;
static st_ui ui;
static st_ui_model model;
static uint8_t frame[1024];
static st_encoder encoder;
static st_button button;
static st_cal_remote cal_remote;
static st_cal_ui cal_ui;
static st_journal journal;
static st_pairing pairing;
static AutoLink auto_link;
static st_console console;
static st_flash_window flash_window;
static volatile bool adc_done;
static volatile uint16_t adc_raw;
static bool paired, change_pair, adc_busy, oled_reported;
static uint32_t last_ms, voltage_ms, adc_started, render_ms;
static uint32_t auto_connect_ms;
static uint64_t uptime_ms;
static st_voltage supply;
static st_voltage_view supply_view;
static const st_voltage_policy supply_policy = {ST_VDDA_DISPLAY_LEVELS_MV, ST_VDDA_HYSTERESIS_MV,
                                                ST_VDDA_FRESH_MS, true};
static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    return HAL_GetTick();
}
static void emit(void *ctx, const char *s)
{
    (void)ctx;
    (void)st_debug_write(&debug, (const uint8_t *)s, strlen(s), HAL_GetTick());
}
static void fatal(void)
{
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
    g.Speed = GPIO_SPEED_FREQ_HIGH;
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
    /* Same dead-battery strobe as ST's G071 examples. Keep physical PA9/10;
     * PA11/12 reservations are not USART remap destinations. */
    HAL_SYSCFG_StrobeDBattpinsConfig(SYSCFG_CFGR1_UCPD1_STROBE | SYSCFG_CFGR1_UCPD2_STROBE);
    CLEAR_BIT(SYSCFG->CFGR1, SYSCFG_CFGR1_PA11_RMP | SYSCFG_CFGR1_PA12_RMP);
}
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef o = {0};
    RCC_ClkInitTypeDef c = {0};
    need(HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1));
    o.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    o.HSIState = RCC_HSI_ON;
    o.HSIDiv = RCC_HSI_DIV1;
    o.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    o.PLL.PLLState = RCC_PLL_OFF;
    need(HAL_RCC_OscConfig(&o));
    c.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    c.AHBCLKDivider = RCC_SYSCLK_DIV1;
    c.APB1CLKDivider = RCC_HCLK_DIV1;
    need(HAL_RCC_ClockConfig(&c, FLASH_LATENCY_0));
    RCC_PeriphCLKInitTypeDef p = {0};
    p.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    p.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
    need(HAL_RCCEx_PeriphCLKConfig(&p));
}
void HAL_SPI_MspInit(SPI_HandleTypeDef *h)
{
    if (h->Instance == SPI1)
    {
        __HAL_RCC_SPI1_CLK_ENABLE();
        gpio(GPIOB, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5, GPIO_MODE_AF_PP, GPIO_NOPULL,
             GPIO_AF0_SPI1);
    }
}
void HAL_I2C_MspInit(I2C_HandleTypeDef *h)
{
    if (h->Instance == I2C1)
    {
        __HAL_RCC_I2C1_CLK_ENABLE();
        gpio(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_MODE_AF_OD, GPIO_NOPULL, GPIO_AF6_I2C1);
        irq(I2C1_IRQn, 1);
    }
}
void HAL_TIM_Encoder_MspInit(TIM_HandleTypeDef *h)
{
    if (h->Instance == TIM2)
    {
        __HAL_RCC_TIM2_CLK_ENABLE();
        gpio(GPIOA, GPIO_PIN_0 | GPIO_PIN_1, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_AF2_TIM2);
    }
}
void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    (void)h;
    __HAL_RCC_ADC_CLK_ENABLE();
    irq(ADC1_COMP_IRQn, 2);
}
static void board_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_15, GPIO_PIN_SET);
    gpio(GPIOA, GPIO_PIN_8 | GPIO_PIN_15, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    gpio(GPIOB, GPIO_PIN_7, GPIO_MODE_INPUT, GPIO_NOPULL, 0);
    gpio(GPIOB, GPIO_PIN_6, GPIO_MODE_IT_RISING, GPIO_NOPULL, 0);
    irq(EXTI4_15_IRQn, 0);
    gpio(GPIOA, GPIO_PIN_2, GPIO_MODE_INPUT, GPIO_PULLUP, 0);
    rf_spi.Instance = SPI1;
    rf_spi.Init.Mode = SPI_MODE_MASTER;
    rf_spi.Init.Direction = SPI_DIRECTION_2LINES;
    rf_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    rf_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    rf_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    rf_spi.Init.NSS = SPI_NSS_SOFT;
    rf_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    rf_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    rf_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    rf_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    rf_spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    need(HAL_SPI_Init(&rf_spi));
    i2c.Instance = I2C1;
    i2c.Init.Timing = 0x00303D5Bu;
    i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    need(HAL_I2C_Init(&i2c));
    need(HAL_I2CEx_ConfigAnalogFilter(&i2c, I2C_ANALOGFILTER_ENABLE));
    need(HAL_I2CEx_ConfigDigitalFilter(&i2c, 0));
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    gpio(GPIOB, GPIO_PIN_0, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0);
    encoder_timer.Instance = TIM2;
    encoder_timer.Init.Prescaler = 0;
    encoder_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    encoder_timer.Init.Period = 65535;
    encoder_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    TIM_Encoder_InitTypeDef e = {0};
    e.EncoderMode = TIM_ENCODERMODE_TI12;
    e.IC1Polarity = TIM_ICPOLARITY_RISING;
    e.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    e.IC1Prescaler = TIM_ICPSC_DIV1;
    e.IC1Filter = 8;
    e.IC2Polarity = TIM_ICPOLARITY_RISING;
    e.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    e.IC2Prescaler = TIM_ICPSC_DIV1;
    e.IC2Filter = 8;
    need(HAL_TIM_Encoder_Init(&encoder_timer, &e));
    need(HAL_TIM_Encoder_Start(&encoder_timer, TIM_CHANNEL_ALL));
    vref_adc.Instance = ADC1;
    vref_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
    vref_adc.Init.Resolution = ADC_RESOLUTION_12B;
    vref_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    vref_adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    vref_adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    vref_adc.Init.LowPowerAutoWait = DISABLE;
    vref_adc.Init.LowPowerAutoPowerOff = DISABLE;
    vref_adc.Init.ContinuousConvMode = DISABLE;
    vref_adc.Init.NbrOfConversion = 1;
    vref_adc.Init.DiscontinuousConvMode = DISABLE;
    vref_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    vref_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    vref_adc.Init.DMAContinuousRequests = DISABLE;
    vref_adc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    vref_adc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;
    vref_adc.Init.OversamplingMode = DISABLE;
    need(HAL_ADC_Init(&vref_adc));
    need(HAL_ADCEx_Calibration_Start(&vref_adc));
    ADC_ChannelConfTypeDef a = {0};
    a.Channel = ADC_CHANNEL_VREFINT;
    a.Rank = ADC_RANK_CHANNEL_NUMBER;
    a.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    need(HAL_ADC_ConfigChannel(&vref_adc, &a));
}
static bool idle(void *ctx)
{
    (void)ctx;
    return !client.pending && !ignition.pending && !radio.tx_active && !radio.tx_pending &&
           supply.valid && supply.mv >= ST_VDDA_MIN_MV && supply.mv <= ST_VDDA_MAX_MV &&
           supply_view.age_ms <= ST_VDDA_FRESH_MS;
}
static void changed(void *ctx)
{
    (void)ctx;
    change_pair = true;
}
static void status(void *ctx)
{
    (void)ctx;
    char s[120];
    (void)snprintf(s, sizeof s, "LINK %u RF %u OLED %u NV %u VDDA %lu VALID %u\r\n",
                   (unsigned)client.link, (!radio.failed && radio.phase >= 13) ? 1u : 0u,
                   oled.failed ? 0u : 1u, (unsigned)pairing.store.status, (unsigned long)supply.mv,
                   supply.valid ? 1u : 0u);
    emit(NULL, s);
    emit(NULL, ignition.auth.key_bound ? "IGN PROJECT CHECK READY / NO EXTERNAL FEEDBACK\r\n"
                                       : "IGN WAIT AUTO LINK\r\n");
}
static IgnitionKeyResult Ignition_SetConsoleKey(void *ctx, const uint8_t key[16])
{
    (void)ctx;
    if (!paired || !idle(NULL) || link.length)
        return IGNITION_KEY_BUSY;
    client.link = ST2_LINK_DOWN;
    client.have_status = false;
    client.epoch = 0;
    (void)IgnitionClient_SetSession(&ignition, 0, false);
    return IgnitionClient_SetKey(&ignition, key);
}
static void yield(void *ctx)
{
    (void)ctx;
    st_debug_service(&debug);
    __WFI();
}
static void radio_init(void)
{
    st_radio_init(&radio, st_radio_hal_port(&radio_hal), st_ref_radio_config(true));
    client.driver_fault = radio.failed;
    (void)st_link_runtime_init(&link, &radio);
    link.auto_link = &auto_link;
}
static void client_init(void)
{
    paired = st_nv_client_init(&client, &pairing.store, st_ref_link_timing());
    st2_timing timing = st_ref_link_timing();
    IgnitionClient_Init(&ignition, pairing.store.identity.peer_uid, (const uint8_t *)UID_BASE,
                        timing.ack_rto_ms, timing.operation_ms, timing.attempts);
    if (paired && IgnitionClient_SetKey(&ignition, st_ref_ignition_key()) != IGNITION_KEY_OK)
        fatal();
    client.ignition = &ignition;
    ignition.transaction_source = &client.next_command;
    st_cal_remote_init(&cal_remote, &client, NULL, now_ms);
    st_cal_ui_init(&cal_ui, st_cal_remote_port(&cal_remote));
    st_ui_attach_calibration(&ui, &cal_ui);
}
static void voltage_step(uint32_t ms)
{
    if (adc_done)
    {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        uint16_t raw = adc_raw;
        adc_done = false;
        __set_PRIMASK(mask);
        (void)HAL_ADC_Stop_IT(&vref_adc);
        adc_busy = false;
        uint32_t mv = 0;
        bool valid = st_ref_vdda_mv(*VREFINT_CAL_ADDR, VREFINT_CAL_VREF, raw, &mv);
        supply = (st_voltage){mv, uptime_ms * 1000, true, valid};
    }
    if (adc_busy && (uint32_t)(ms - adc_started) > 20)
    {
        (void)HAL_ADC_Stop_IT(&vref_adc);
        adc_busy = false;
        supply.valid = false;
    }
    if (!adc_busy && (uint32_t)(ms - voltage_ms) >= 100)
    {
        voltage_ms = ms;
        adc_started = ms;
        adc_busy = HAL_ADC_Start_IT(&vref_adc) == HAL_OK;
        if (!adc_busy)
            supply.valid = false;
    }
    supply_view = st_voltage_evaluate(&supply_policy, &supply, uptime_ms * 1000, supply_view);
}
static void event(st_ui_event e, uint32_t ms)
{
    bool connect = e == ST_UI_PRESS && ui.page == ST_UI_STATUS && ui.selection == 7;
    if (connect && radio.failed && !radio.tx_active)
        radio_init();
    if (connect && !paired)
    {
        ui.notice = ST_UI_NOTICE_CONNECT;
        return;
    }
    (void)st_ui2_handle(&ui, e, &client, ms);
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
    emit(NULL, "SwiftThrust CTRL REFERENCE; INFO / STATUS\r\n");
    st_ui_init(&ui);
    st_g0_oled_init(&oled_port, &i2c, HAL_GetTick, true);
    st_oled_init(&oled, st_g0_oled_i2c(&oled_port), HAL_GetTick());
    flash_window = (st_flash_window){NULL, idle};
    (void)st_journal_open(&journal, st_flash_journal_port(&flash_window));
    (void)st_pairing_open(&pairing, st_journal_storage(&journal), (const uint8_t *)UID_BASE,
                          ST_NV_CONTROLLER);
    AutoLink_Init(&auto_link, &pairing, st_ref_ignition_key(), ST_PROJECT_PAIR_ID, NULL, idle);
    console = (st_console){.pair = &pairing,
                           .emit = emit,
                           .idle = idle,
                           .changed = changed,
                           .status = status,
                           .ignition_key = Ignition_SetConsoleKey};
    radio_hal = (st_radio_hal){.spi = &rf_spi,
                               .cs = GPIOA,
                               .cs_pin = GPIO_PIN_15,
                               .busy = GPIOB,
                               .busy_pin = GPIO_PIN_7,
                               .reset = GPIOA,
                               .reset_pin = GPIO_PIN_8,
                               .now_ms = now_ms,
                               .yield = yield,
                               .spi_timeout_ms = 5,
                               .busy_timeout_ms = 20,
                               .reset_low_ms = 2,
                               .reset_settle_ms = 20};
    if (!st_radio_hal_bind(&radio_hal, true))
        fatal();
    radio_init();
    client_init();
    last_ms = HAL_GetTick();
    for (;;)
    {
        uint32_t ms = HAL_GetTick();
        uptime_ms += (uint32_t)(ms - last_ms);
        last_ms = ms;
        voltage_step(ms);
        change_pair |= AutoLink_TakeChanged(&auto_link);
        if (change_pair)
        {
            change_pair = false;
            client_init();
        }
        int32_t delta = st_encoder_update(&encoder, (uint16_t)__HAL_TIM_GET_COUNTER(&encoder_timer),
                                          ST_ENCODER_EDGES_PER_STEP);
        if (delta > 8)
            delta = 8;
        if (delta < -8)
            delta = -8;
        while (delta > 0)
        {
            event(ST_UI_NEXT, ms);
            delta--;
        }
        while (delta < 0)
        {
            event(ST_UI_PREV, ms);
            delta++;
        }
        StButtonEvent key_event =
            Button_Update(&button, HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2) == GPIO_PIN_RESET, ms);
        if (key_event == ST_BUTTON_SHORT)
            event(ST_UI_PRESS, ms);
        else if (key_event == ST_BUTTON_LONG)
            event(ST_UI_BACK, ms);
        if (paired)
        {
            if (!radio.failed && idle(NULL) &&
                (uint32_t)(ms - auto_connect_ms) >= ST_LINK_RECONNECT_MS &&
                (client.link == ST2_LINK_DOWN ||
                 (client.link == ST2_LINK_UP && !st2_client_fresh(&client, ms))))
            {
                auto_connect_ms = ms;
                (void)st2_client_connect(&client, ms);
            }
            st_ui2_service(&ui, &client, ms);
            st_cal_remote_step(&cal_remote, ui.page == ST_UI_CALIBRATION);
        }
        st_link_client_step(&link, &client, ms, ms % 101u);
        HAL_GPIO_WritePin(
            GPIOB, GPIO_PIN_0,
            Link_GetLed(Link_GetHealth(client.have_status, client.received_ms, ms, radio.failed),
                        ms)
                ? GPIO_PIN_SET
                : GPIO_PIN_RESET);
        st_oled_service(&oled, ms);
        if ((uint32_t)(ms - render_ms) >= 100)
        {
            render_ms = ms;
            st_ui2_render(&ui, &client, supply_view, ms, &model);
            st_ui_raster(&model, frame);
            (void)st_oled_present(&oled, frame);
        }
        if (oled.failed && !oled_reported)
        {
            emit(NULL, "OLED FAULT; CHECK WIRING THEN RESET\r\n");
            oled_reported = true;
        }
        uint8_t byte;
        for (unsigned i = 0; i < 32 && st_debug_uart_read(&uart, &byte); i++)
            st_console_feed(&console, byte);
        st_debug_service(&debug);
        __WFI();
    }
}
void SysTick_Handler(void)
{
    HAL_IncTick();
}
void EXTI4_15_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
}
void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == GPIO_PIN_6)
        st_radio_irq_isr(&radio);
}
void I2C1_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&i2c);
    HAL_I2C_ER_IRQHandler(&i2c);
}
void ADC1_COMP_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&vref_adc);
}
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&uart.uart);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
    if (h == &vref_adc)
    {
        adc_raw = (uint16_t)HAL_ADC_GetValue(h);
        adc_done = true;
    }
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *h)
{
    if (h == &vref_adc)
    {
        adc_raw = 0;
        adc_done = true;
    }
}
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h)
{
    st_g0_oled_complete(&oled_port, h);
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    st_g0_oled_error(&oled_port, h);
}
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *h)
{
    st_g0_oled_error(&oled_port, h);
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
