> 历史评审记录：本文中的旧ADC、时钟及引脚假设已由[2026-10-29双板硬件契约](REFERENCE_APPLICATION.md)替代；当前测试/交付见[R8硬件适配报告](R8_HARDWARE_ADAPTATION_20261029.md)。历史结果不代表当前固件或实板验证。

> 历史阶段记录。当前OLED/菜单/协议/实现状态以../HANDOFF.md、OLED_REFERENCE.md和../PROTOCOLS.md为准。

# SwiftThrust v0.1：资源、接口及调度候选

本文件为规划，不是已完成的采集固件。引脚逐项见两端`pin_candidates.csv`，真实CubeMX配置将保存在各端`SwiftThrust_*_Candidate`子目录。引脚与默认时钟仅供审阅；`config_pending.json`中board_bound=false，未定值保持null。原有README保留不改。

## 引脚分组

| 端 | 分组 | MCU管脚/封装位置 | 说明 |
|---|---|---|---|
| 采集 | ADC SPI1 | PA4/20 CS；PA5/21 SCK、PA6/22 MISO、PA7/23 MOSI；PC4/24 DRDY、PC5/25 SYNC | 紧邻管脚便于ADC数字侧布线；CH0/CH1模拟输入在外部ADC上 |
| 采集 | TF SDIO4 | PC8/39 D0、PC9/40 D1、PC10/51 D2、PC11/52 D3、PC12/53 CK、PD2/54 CMD | 先保留此组；卡检测PB5/57为极性待定输入 |
| 采集 | 无线SPI2 | PB12/33 CS、PB13/34 SCK、PB14/35 MISO、PB15/36 MOSI、PC6/37 BUSY、PC7/38 DIO1、PB2/28 NRESET | 避免SPI3占用TF同组引脚 |
| 采集 | 电压/四灯 | PC0/8 MCU ADC1_IN10；PB6/58、PB7/59、PB8/61、PB9/62为电压档位1–4 | 最新要求：四灯均用于电压档位，不再表示原四种状态；阈值/滞回/极性未定，ioc全为输入 |
| 采集 | 时钟/调试 | PA8/41仅预留ADC CLKIN选项；PA13/46、PA14/49 SWD | ADC时钟未启用；PH0/PH1与LSE脚未占用 |
| 遥控 | 无线SPI1 | PA15/26 CS；PB3/27 SCK、PB4/28 MISO、PB5/29 MOSI、PB6/30 DIO1、PB7/31 BUSY；PA8/18 NRESET | 仅对应不带N的G071KBT6 |
| 遥控 | OLED I2C1 | PB8/32 SCL、PB9/1 SDA | 邻近封装角部；地址/上拉/模块复位接法待定 |
| 遥控 | 编码器/按键/电压 | PA0/7 TIM2_CH1、PA1/8 TIM2_CH2；PA2/9按压、PA3/10独立键；PA4/11 ADC1_IN4 | 连续管脚；编码器滤波/按键极性/电压分压待定 |
| 遥控 | 调试 | PA13/24 SWDIO、PA14-BOOT0/25 SWCLK | 保留SWD；NRST/BOOT选项与PC14/15时钟脚不另占 |

布线便利是基于封装相邻管脚的候选判断，尚无板形与器件坐标，不能声称是最终最优走线。无线选定DIO1为IRQ候选；ADC EXTI4、无线EXTI7不相冲突。遥控IRQ为EXTI6；按键暂轮询。若改动需重查同号EXTI线和特殊复用。

## DMA候选（尚未写入ioc）

| 外设 | DMA控制器/Stream/Channel | 方式 |
|---|---|---|
| ADS SPI1 RX | DMA2 / Stream0 / Channel3 | 高优先级，字节宽度12字节；每个DRDY完整一帧 |
| ADS SPI1 TX | DMA2 / Stream5 / Channel3 | NULL/虚字节时钟；不可与RX复用同Stream |
| SDIO RX | DMA2 / Stream3 / Channel4 | SDIO四位；4字节对齐和适当FIFO配置需随HAL/缓存确认 |
| SDIO TX | DMA2 / Stream6 / Channel4 | 单卡读写串行，TF任务独占文件系统 |
| 无线SPI2 RX/TX | DMA1 / Stream3/4 / Channel0（可选） | 与ADC不同控制器；状态队列满就丢状态，不阻塞ADC |
| 本地电压ADC1 | 低频非阻塞读取；若DMA则DMA2/Stream4/Channel0候选 | 与上述分开，不使用ADC1默认Stream0冲突位置 |

映射依据：[RM0383 §9 DMA映射表](https://www.st.com/resource/zh/reference_manual/rm0383-stm32f411xce-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)及本机CubeMX的`DMA-STM32F411_dma_v2_0_Modes.xml`。host检查会验证指定Stream下的请求与Channel是否存在，并检查独占Stream冲突。Stream3与6是分配给同一张卡两方向，不是双卡。某些HAL版本建议一个SDIO双向DMA句柄，落地前按所选HAL版本再次核对，不把此规划当作现成驱动。采集端TIM2预留为无输出的32位时间基，尚未定频率和中断优先级。

## 数据路径和存储预算

2kSPS时ADC线数据为`12×2000=24000 byte/s`，SPI最低纯数据时钟`96×2000=192 kbit/s`；真实SCLK还须预留ISR/DMA/CS间隙。候选32字节日志记录为64kB/s，约230.4MB/小时（十进制，不含文件头/分配开销）；日志格式尚未实现，不是承诺固定格式。提高采样率必须同时重算时钟、CPU、SRAM、TF延迟和无线占空。

规划：DRDY取时间戳/序号 → DMA双帧缓冲（2×12字节） → 单生产者/单消费者环形队列 → 校验/记录 → 两个4KiB对齐写缓存 → 单一TF写入者。RAM候选：1024×32字节环形=32KiB，两个写缓存=8KiB，另留FatFs/驱动/栈/状态空间。32KiB仅相当于2kSPS、32字节记录的512ms容量；将“两个DMA帧”当作TF延迟隔离远远不够，也不能用平均TF写速代替最坏写延迟。高采样率和故障卡可超过任何有限缓冲，策略必须明确：计数并显著标记丢样/停止，不静默覆盖。

DMA读写缓冲生命周期、内存屏障/短临界区和缓存归属须在HAL适配实现。不能只把索引声明volatile就当作并发正确。中断中不写文件、不printf、不轮询无线BUSY、不等SDIO完成。CRC/状态异常记录原值且标为无效，不能生成有效推力。序号断点、时间间隔异常、队列溢出、DMA忙、SPI/卡错误分别计数。

TF采用唯一新文件、分段记录、记录/块校验与周期sync的方案候选，写失败后停止追加并保留原文件，禁止自动格式化/截断/重新打开为覆盖模式。恢复导出只读到最后完整有效记录。**突然断电可能破坏FAT元数据或卡内缓存，软件不能保证已sync文件一定完好**；要求更强保留需电源预警/足够保持能量、卡实测及文件系统恢复策略，当前未验证。必须区分已提交DMA、已写文件、已sync三种进度，显示最后确认进度及尾部风险。

原始日志保留两路有符号码、状态、CRC结果、序号、DRDY时间戳、实际采样率依据、CLKIN/OSR/功耗模式、PGA、激励与分压/传感器标定版本。只有有有效标定才生成N；CH2只有电路与标定确认后才输出V。面向UNDERLINE预留普通UTF-8 CSV（显式time_s/force_N、采样率、单位和标定元数据），不伪造未测数据。缺失样本保持时间断点，不重排成等间隔。尚未获得UNDERLINE版本/导入规范，因此**不宣称已验证兼容**，需一份实际可导入样例再定导出模板。

无线独立二进制状态/采集控制协议待实现，仅允许采集开始/停止与状态查询的候选命令范围；本轮没有可执行命令。RF周期取决于SF/BW实际空中时长与重试预算，不能默认为5Hz或10Hz。只发降采样快照、采集/TF状态、丢样计数与电压新鲜度，不传输2kSPS完整原始流、不让ACK阻塞本地日志；状态积压时保留最新一帧。CRC用于检测误码，不等于身份认证；远程开始/停止后续还需会话/序号/重复包处理。

## FreeRTOS取舍（工程判断，尚未启用）

采集端可选FreeRTOS静态任务，以隔离FatFs或HAL等待：高优先级采集处理、单独TF写入、低优先级无线/状态。ISR只投递最小事件；DRDY从硬件边沿来，不能由RTOS tick定2kSPS。静态栈、TCB、通知/队列避免运行期分配；初步额外栈预算约8–16KiB只是待测估算，必须用链接map和栈水位核实。若采用RTOS，HAL时基与tick设计、可调用FromISR的NVIC优先级上限必须核对；高于RTOS系统调用阈值的ISR不能调用内核API。参考[FreeRTOS静态分配](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation)与[中断优先级说明](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/02-Customization)。

裸机也可达成：高优先级DRDY/SPI DMA生产者不受主循环TF等待影响，环形缓冲足够时可暂存。但同步f_write/f_sync可能长时间不返回，造成主循环无法及时校验/搬运/更新状态；必须把完整帧转移生产者与存储消费者合理分层，测量最坏阻塞并有超时/溢出策略。若全路径可拆为非阻塞状态机，裸机更省栈、更易审计。RTOS不能缩短卡忙时间，也不能补救长临界区或不正确DMA所有权。当前建议：先用HAL原型测SDIO/FatFs阻塞，再决定采集端是否启用静态FreeRTOS；ioc保持无RTOS，接口不绑定调度器。

遥控端优先事件循环：编码器/按键事件、异步无线、分片OLED刷新与低频电压。128×64单色帧缓冲约1024字节；400kHz I2C整帧理想下限约23ms，加协议开销更长，按键/RF不应在整屏阻塞刷新期间失去处理。具体OLED分辨率、I2C速率、地址待模块确认。G071按32KiB带硬件校验的SRAM保守预算（36KiB需不使用校验位），不预先承担多个任务栈。1.3寸OLED的128×64只是布局候选，控制器待确认，不能混用SSD1306/SH1106/SH1107驱动。

## 电压UI合同

同时显示“遥控电池”和“采集供电”，每路包含测量有效性/数据年龄；阈值与新鲜度超时由电路、电芯和RF周期决定。未接检测=未知、过期=过期、欠压=明显告警，不能把0/无数据当满电。接收到新RF包不代表其中电压是新采样值，须携带测量年龄/有效标记。电压只能说明该测点电压，不能据此声称电池健康/内阻正常/还能完成某动作。

未来独立点火电源与前两者明确区分，但当前无检测电路，始终未知，不虚构第三路实测电压。联动/仅发射模式标为“未实现，安全审查中”；没有点火管脚、驱动、无线命令或输出API。

补充核对：具体共模公式/页码、已收敛模拟供电候选、真实时钟状态、全速率缓冲预算和SDK体积见[FOLLOWUP_REVIEW_v0.1.md](FOLLOWUP_REVIEW_v0.1.md)。

2026-10-20交接冻结：最新要求及实际完成边界以根目录[HANDOFF.md](../HANDOFF.md)为入口；协议和菜单要求分别见[协议草案](ACQUISITION_PROTOCOL_DRAFT.md)与[UI要求](CONTROLLER_UI_REQUIREMENTS.md)，均未实现。
