# Controller ReferenceApp（STM32G071KBT6）

当前可执行入口是 `board/reference_main.c`，EIDE 选择 `ReferenceApp`。硬件契约来自 2026-10-29 的 Controller epro2。内部 HSI16 提供 16 MHz SYSCLK/HCLK/PCLK；SPI1 分频 4，4 MHz；USART1 为 115200 8N1；I²C1 使用 HSI16 和 `0x00303D5B`，标称约 100 kHz，实际边沿/频率须上板测量。

| 功能 | GPIO / 外设 |
|---|---|
| LoRa SCK/MISO/MOSI | PB3/PB4/PB5，SPI1 AF0 |
| LoRa CS/BUSY/DIO1/NRESET | PA15/PB7/PB6/PA8 |
| OLED SCL/SDA | PB8/PB9，I²C1 AF6，开漏，无内部上拉；模块外部上拉 |
| 编码器 A/B/按下 | PA0/PA1，TIM2 AF2；PA2 低有效，消抖与长短按保留 |
| 本地电池 | PA3 / ADC1_IN3，10kΩ + 10kΩ，100nF |
| 链路灯 | PB0，高有效 |
| USART1 TX/RX | 实体 PA11[PA9] / PA12[PA10]，22/23 脚；逻辑 PA9/PA10 AF1 |
| SWD | PA13/PA14 |

`HAL_MspInit` 设置 `SYSCFG_CFGR1.PA11_RMP` 和 `PA12_RMP`。根据 [ST RM0444](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)，实体 PA11/12 由逻辑 GPIO PA9/10 控制，原 PA9/10 引脚转为模拟模式。保留 UCPD dead-battery strobe；串口 IRQ、收发队列及协议保持原有路径。

ADC 每 100 ms 先测 VREFINT 算出 VDDA，再测 PA3。ADC 时钟 PCLK/2 = 8 MHz，160.5 周期采样，通道通过可配置序列的 `ADC_REGULAR_RANK_1` 切换。中断只保存结果，主循环换算电池电压 `code × VDDA × 2 / 4095`，用 1/4 IIR 滤波；超时、错误、越界、失效或陈旧 VDDA 会使电池读数无效。电池分压契约独立于采集板配置。

GUI 状态页显示“遥控电池”和“采集电池”；本地 VDDA 只用于 NV 写入安全检查，并在 UART STATUS 单独显示，不能当作电池电压。电量档位是负载电压估计，不宣称精确 SOC。四行菜单、R8 协议、重试/重连、标定与 ARM/FIRE/DISARM 保留。

候选 IOC 同步 PA3 与 PA11/12 重映射；真实固件来源仍为 ReferenceApp。`include/st_controller_bsp.h` 是历史未绑定接口草案，不参与 ReferenceApp。NV page62/page63 和升级边界不变。

离线测试不能替代上板检查：核对串口 22/23 脚收发、PA3 与实际电池/VDDA 电压、OLED SCL/SDA 上拉和时序、编码器方向/机械消抖、无线收发与 PB0 指示。未进行烧录或实物负载测试。
