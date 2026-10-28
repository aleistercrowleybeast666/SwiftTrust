# SwiftThrust R8 双板应用：2026-10-29 硬件契约

当前运行目标为两端 EIDE **ReferenceApp**，初始化入口为各自 `board/reference_main.c`。硬件依据用户提供的两份 2026-10-29 epro2，固件直接适配这些原理图。候选 IOC 已同步用于资源检查，不是运行代码的生成入口。LinkCheck 是历史组件链接目标，不能烧录为应用。

本次只进行了源码修改、CAD 解析、离线模型测试和目标编译。没有访问探针、串口、实板或执行负载；CAD 接线确认不表示电气、噪声、吞吐或输出安全测试通过。

## 硬件来源与 GPIO

`hardware_acquisition_20261029.json`、`hardware_controller_20261029.json` 保存文件 SHA256、器件、供电和引脚网络；`extract_epro_contract.py` 读取 EPRU 最新票据，排除删除图元和失效导线组，按符号引脚连接端、旋转、镜像、导线端点和网络名解析，不因线段内部交叉判断连接。`check_hardware_contract.py --source-dir <下载目录>` 可重放来源核对。两块 PCB 文档仅含元数据，本次依据原理图，不声称核验了铜皮布线。

| 功能 | 采集 STM32F411RCT6 | 遥控 STM32G071KBT6 |
|---|---|---|
| ADC SPI | PA4 CS；PA5 SCK / PA6 MISO / PA7 MOSI，SPI1 AF5 mode1 | PA3 / ADC1_IN3 为电池检测 |
| ADC DRDY / CLK | PC4 EXTI4 下降沿；PA8 AF0 MCO1 HSE/2 | — |
| PC5 | 未连接，保持模拟输入；不是 ADC RESET/SYNC | — |
| LoRa SPI | PB12 CS；PB13 SCK / PB14 MISO / PB15 MOSI，SPI2 AF5 | PA15 CS；PB3 SCK / PB4 MISO / PB5 MOSI，SPI1 AF0 |
| LoRa BUSY / DIO1 / NRESET | PC6 / PC7 EXTI7 上升沿 / PB2 | PB7 / PB6 EXTI6 上升沿 / PA8 |
| TF SDIO | PC8..PC11 DAT0..3 / PC12 CLK / PD2 CMD，AF12 | — |
| TF 卡检测 | PB5，低有效、内部上拉 | — |
| 电池 | PC0 ADC1_IN10；R14/R15=10k/10k、C19=100nF | PA3 ADC1_IN3；R7/R8=10k/10k、C7=100nF |
| 电量 LED | PB6..PB9，高有效；D5 是硬件电源灯 | GUI 显示本地电池电压 |
| 链路 LED | PB10，高有效 | PB0，高有效 |
| 外部执行输出 | PA0 AF1 TIM2_CH1；高有效、上电低 | — |
| USART1 115200 8N1 | PA9 TX / PA10 RX，AF7 | 逻辑 PA9 TX / PA10 RX AF1；SYSCFG PA11_RMP/PA12_RMP 置位，映射到物理22/23脚 PA11[PA9]/PA12[PA10] |
| SWD | PA13 / PA14 | PA13 / PA14 |
| OLED SSD1306 128×64 | — | PB8 SCL / PB9 SDA，I2C1 AF6 开漏，无内部上拉，模块外部上拉 |
| 编码器 | — | PA0/PA1 TIM2 AF2；PA2按键低有效，30ms去抖/800ms长按 |
| HSE | PH0/PH1，8.192MHz无源晶振 | HSI16内部16MHz |

精确封装位置和未使用脚见 `reference_pins.csv`。采集 ADC SPI/DRDY 的 R35..R39 为47Ω，PA8串联R16为33Ω。桥式传感器激励、ADS1220 AVDD和REFP0同源5V，AVSS/REFN0接地，DVDD=3.3V。桥信号经滤波接 AIN1正 / AIN2负；AIN0/AIN3未用于第二路测量。供电器件包括 LTC3440 3.3V、采集端 TPS61040 5.4V及TPS7A2050 5V；这些是器件/网络事实，不是电源性能验证。

## F411 时钟树

| 节点 | 配置/频率 |
|---|---|
| HSE_VALUE | 8,192,000Hz；HSE_ON无源晶振模式 |
| PLL M/N/P/Q | 8 / 375 / 4 / 8 |
| PLL输入 / VCO | 1.024MHz / 384MHz |
| SYSCLK / HCLK | 96MHz / 96MHz |
| PCLK1 / PCLK2 | 48MHz / 96MHz |
| APB1定时器 | 96MHz；TIM5与TIM2分频95，计数1MHz |
| MCO1 | HSE/2=4.096MHz，PA8 AF0 |
| SPI1 ADC / SPI2 LoRa | 96MHz/32=3MHz / 48MHz/16=3MHz |
| ADC1内部电池/VDDA | PCLK2/4=24MHz，480周期采样约20µs |
| SDIO PLLQ域 | 48MHz；识别400kHz，数据48MHz/(2+2)=12MHz，四位 |
| USART1 | PCLK2=96MHz，HAL按115200 8N1计算BRR |
| 电压调节 / Flash | Scale1 / 3等待周期，板级3.3V供电 |

核对依据：[STM32F411 RM0383](https://www.st.com/resource/en/reference_manual/dm00119316.pdf) 的Flash等待周期表、RCC PLL/总线/MCO和SDIO分频规则，以及[DS10314](https://www.st.com/resource/en/datasheet/stm32f411ce.pdf)的电气条件。1.024MHz PLL输入、384MHz VCO、96MHz内核和48/96MHz总线均在允许范围；96MHz不可沿用Scale2或2WS。初始化检查HSE/PLL就绪、实际HAL频率及MCO寄存器，失败进入关闭执行输出的故障状态，不启动ADC。运行期间开启CSS；HSE丢失由NMI立即将PA0切回GPIO低、丢弃在途ADC样本并锁定时钟故障，前台尽可能排空既有有效记录，禁止新的START、执行授权或NV写入。VOSRDY确认后才切换96MHz。TIM5仅提供时间戳，ADC转换时钟来自MCO，二者不能混用。

## ADS1220 与数据链路

官方依据：[TI ADS1220 SBAS501D](https://www.ti.com/lit/ds/symlink/ads1220.pdf)，包括SPI时序、输入/PGA共模范围、寄存器和表8-4转换时序。

默认寄存器 `31 D4 40 00`：AIN1-AIN2，gain1且PGA旁路，Turbo DR=110、连续转换，REFP0/REFN0外部参考，温度/烧断电流/IDAC关闭。外部时钟由CLK脚自动检测，芯片没有“外部时钟选择寄存器”、ID寄存器、专用RESET引脚或输出CRC。初始化按RESET→默认值读回→WREG→全寄存器RREG校验→START运行；有SPI/超时/读回失败就不交接采集。

标称数据率2000SPS；4.096MHz下每次稳定连续转换2072时钟，即505.859375µs、约1976.833977SPS。START后的首转换按114+2072时钟保守等待；单次转换表列2130时钟不应当作连续DRDY周期。软件在约500ms窗口统计真实DRDY边沿，以理论值±2%做就绪检查；HSE同时驱动ADC及计数器，因此不能据此测出晶振绝对误差。

DRDY中断只取TIM5时间戳、分配序号并启动3字节SPI DMA；完成中断将原始24位数据零填充到32B SRAM队列项，做轻量队列投递，不执行FatFs/标定计算。32KiB环形队列和两个4KiB落盘缓冲保留。缺失转换只增加缺失统计/序号，不补造样本。真实边沿计数与推算缺失序号分开；记录有效性、饱和、丢失、时间异常分别标记。

DRDY无边沿10ms、DMA超过1ms、SPI错误或异常时间间断会锁定采样源故障，阻止START并停止当前记录。先停止入队、排空、写END、sync/close，再在SPI安全空闲时发送POWERDOWN。复位后完整初始化；驱动也提供显式POWERDOWN/重新初始化接口；SPI传输故障后须先完成abort并重新绑定HAL，再由调用方重新初始化，不自动恢复录制。正常STOP保留ADC连续运行，供空闲速率检查和标定使用。

NV写入只在空闲、非标定和执行输出关闭时允许；先停止新的ADC DMA并等在途事务完成，写入后恢复真实DRDY边沿监测并重新验速。不会把软件看到DRDY低电平的时刻伪装成采样时刻。TF扇区等待继续通过有重入保护的fast_service服务ADC队列、电压、UART和执行截止；40KiB缓冲并不保证任意慢卡无限持续采样。

## 日志、传感器和标定

STLOG001保持512B头和32B记录，在原保留区84..127增加 `ADC1` 扩展，标明型号1220、单通道、signed24、参考/激励、PGA/旁路、输入端、时钟、标称/理论及开始前测得的DRDY频率。旧ch1槽写INT32_MIN明确表示不存在；导出CSV的第二通道/ADC状态列为空，不将它解释为零电压。文件记录仍有CRC32，ADC本身没有CRC。旧双通道STLOG001和SZR1会话置零头继续可读；细节见 `../PROTOCOLS.md`。

`SENSOR.CFG` 必须显式给出传感器身份、额定力、5V激励许可、灵敏度、PGA/旁路和共模范围。范围检查同时覆盖差分满量程及PGA输出余量；没有有效配置时保持gain1旁路记录原始码，不输出准确推力。配置不产生或测量激励电压。5V参考的输入电压换算是raw×5V/(2^23×gain)，桥式mV/V在同源激励/参考下按比例测量。

CAL0.BIN/CAL1.BIN保留封印、CRC、generation、sync/close/readback后提交。ADC型号、参考、输入、PGA及配置摘要绑定到标定；旧ADC或不同配置的系数不直接应用。新标定的保存不会先删掉旧有效槽，原始ADC码始终不变。永久零点、永久斜率、可选会话置零、自动置零设置和取消/保存流程继续保留，详见 `CALIBRATION.md` 和 `SENSOR.CFG.example`。

## 遥控端与原有行为

G071保持HSI16，LoRa SPI1=4MHz；OLED使用HSI16内核和原timing=0x00303D5B。PA3电池与VREFINT交替测量，使用工厂VREFINT校准计算VDDA，按1:2分压还原电池，约10Hz刷新、1/4 IIR滤波；超时/错误/陈旧数据标记未知。GUI本地电池、远端采集电池和维护串口的VDDA互不混用。电量档位只是电压估计，不是精确SOC。

R8基础帧、Pair ID/UID、session/transaction去重、HELLO/STATUS/QUERY/COMMAND/RESULT/DATA、重试和连接恢复保持。两端射频均为2473MHz、+12dBm、SF10、BW812.5kHz、CR4/5，原CRC/LNA/AGC设置保留。STOP在TF落盘收尾完成后才确认终态。START/重试/重连不会自动触发执行输出。

PA0保留上电低、ARM许可10s、独立FIRE确认、单次1000ms TIM2 PWM2/OPM自主截止、STOP/故障提前关闭和去重。当前硬件输出启用沿用用户此前确认的阻性负载配置；没有执行实物负载测试，不能由模拟断言推定实际MOSFET关闭或输出安全。

## NV 与升级

| MCU | 保留擦除区 | 应用范围 |
|---|---|---|
| F411RCT6 | sector2 0x08008000..0x0800BFFF；sector3 0x0800C000..0x0800FFFF | 向量0x08000000；其余加载从0x08010000起 |
| G071KBT6 | page62 0x0801F000..0x0801F7FF；page63 0x0801F800..0x0801FFFF | 前124KiB |

配对、计数和Flash日志格式未改变，不清空NV解决迁移。构建检查加载段不覆盖NV，dist仅交付稀疏HEX/ELF，不生成跨NV空洞的BIN。升级必须保留这两个区；本任务没有整片擦除、烧录或改Option Bytes。

## 构建与检查

环境使用现有SDK_ROOT、ARM_GCC_ROOT、HOST_CC及EIDE扩展，不复制SDK。执行：`run_host_tests.py`（另加 `--english`）、`check_link_runtime.py`、`check_reference.py`、`check_ignition_f4.py`、`check_ads1220_capture.py`、`check_clock_fault.py`、`check_hardware_contract.py`、`validate_candidates.py`、`build_reference.py --artifacts`。均在Models目录，建议 `python -X utf8 -B`。

测试结果与交付范围见 `R8_HARDWARE_ADAPTATION_20261029.md`。上板仍须测HSE/MCO/DRDY/SPI波形、实际持续记录率与TF最坏延迟、传感器共模/噪声/饱和与真实载荷标定、双端电池误差、OLED/编码器/UART、射频丢包/重连、STOP文件收尾、掉电NV保护和PA0启动/截止波形。
