# R8 双端固件硬件适配交付报告 — 2026-10-29

两端实际 EIDE `ReferenceApp` 已按最新原理图完成源码适配，并通过主机回归、原生 CubeMX 资源检查及 ARM 目标编译。构建产物位于本地 `dist/`。本次没有连接实物、烧录、擦除 Flash 或执行负载测试；以下测试结论仅覆盖离线可验证范围。

## 硬件依据

| 文件 | SHA256 |
|---|---|
| ProDoc_Acquisition_1_2026-10-29.epro2 | `93c13da6f8ed06adad0df86d626386deeba7012eeb5880fc76941af8a1a868b8` |
| ProDoc_Controller_1_2026-10-29.epro2 | `c652689a26c2d2e398be7d99cac7d58ec9cadf36b0649a3d92bef38d3ffbb14f` |

解析器保存最新有效器件和电气网络，处理符号旋转、镜像、删除票据、导线组和网络标签；不把线段内部的视觉交叉当作短接。快照见 [采集端](hardware_acquisition_20261029.json) / [遥控端](hardware_controller_20261029.json)。实际重新解析输入文件后，快照完全一致。SWD、PC4/PC5网络保持独立，ADC SPI串联47Ω及MCO串联33Ω均已核对。本次文件内PCB文档只有元数据，因此结论依据原理图，不代表已检查PCB铜皮。

## 实际修改的运行源码

| 范围 | 文件 | 作用 |
|---|---|---|
| 公共配置 | `Common/Inc/st_project_config.h` | ADS1220时钟、参考、转换周期和单通道默认配置；遥控电池电路配置 |
| 采集板入口 | `Acquisition/board/reference_main.c`、`stm32f4xx_hal_conf.h` | 8.192MHz HSE/96MHz时钟、MCO、PB10、ADC配置接入、真实DRDY统计、CSS故障封锁 |
| ADC驱动 | `Acquisition/src/st_ads.c`、`include/st_ads.h` | ADS1220命令、寄存器校验、增益/共模检查、转换时序和恢复接口 |
| ADC HAL/采集 | `Acquisition/ports/st_ads_hal.c/.h`、`st_f4_capture.c/.h` | 3字节DMA、CS时序、错误/超时、丢样、时钟故障、空闲NV操作前后的暂停/恢复 |
| 原始数据及日志 | `Acquisition/src/st_core.c`、`st_logger.c`、`include/st_core.h` | signed24、缺失通道标记、ADC1元数据及有效性 |
| 配置及标定 | `Acquisition/src/st_reference.c`、`st_cal.c`、`include/st_reference.h`、`st_cal.h` | SENSOR.CFG电气配置、ADC绑定、旧标定失效和新配置匹配 |
| 标定持久化 | `Acquisition/ports/st_cal_fatfs.c/.h`、`st_cal_system.c` | 保留双槽提交；配置迁移时允许重新标定并安全保存 |
| 输出保护 | `Acquisition/ports/st_ignition_f4.c/.h` | CSS/NMI立即断开PA0定时器复用并拉低，故障锁定到复位 |
| 电池及板级定义 | `Acquisition/src/st_battery.c`、`include/st_battery.h`、`st_bsp.h` | 独立电池电路有效性接口、采集GPIO契约 |
| 遥控入口及BSP | `Controller/board/reference_main.c`、`include/st_controller_bsp.h` | PA11/PA12重映射、PA3与VREFINT交替测量、独立电池滤波/失效状态 |
| 遥控UI | `Controller/include/st_ui.h`、`src/st_ui2.c`、`src/st_font_subset.inc` | 显示实际遥控电池；补充“遥”字，保留四行和原操作方式 |
| 构建 | `Controller/.eide/eide.yml` | 将共享电池模块加入实际目标 |

同时更新两端IOC、CubeMX脚本、引脚CSV、配置快照、字体来源、Python导出工具及相关测试。原R8通信、配对、任务和Flash日志模块保持既有实现；历史硬件说明增加明确历史标识，当前事实集中在 [REFERENCE_APPLICATION.md](REFERENCE_APPLICATION.md)。

## 双端最终 GPIO

| 功能 | 采集 STM32F411RCT6 | 遥控 STM32G071KBT6 |
|---|---|---|
| ADC CS/SCK/MISO/MOSI | PA4/PA5/PA6/PA7，SPI1 AF5 mode1 | — |
| ADC DRDY/CLK | PC4 EXTI4下降沿 / PA8 AF0 MCO1 | — |
| PC5 | 未连接，模拟输入，无RESET/SYNC用途 | — |
| LoRa CS/SCK/MISO/MOSI | PB12/PB13/PB14/PB15，SPI2 AF5 | PA15/PB3/PB4/PB5，SPI1 AF0 |
| LoRa BUSY/DIO1/NRESET | PC6/PC7/PB2 | PB7/PB6/PA8 |
| TF | PC8..PC11 DAT0..3，PC12 CLK，PD2 CMD，AF12；PB5检测低有效 | — |
| 电池 | PC0 ADC1_IN10，10k/10k分压 | PA3 ADC1_IN3，10k/10k分压，100nF |
| 电量LED | PB6..PB9，高有效 | 显示本地电池电压 |
| 链路LED | PB10，高有效 | PB0，高有效 |
| 执行输出 | PA0 TIM2_CH1 AF1，上电低 | — |
| USART1 | PA9 TX / PA10 RX AF7 | 逻辑PA9 TX / PA10 RX AF1；映射到物理22/23脚 PA11[PA9] / PA12[PA10] |
| SWD | PA13/PA14 | PA13/PA14 |
| OLED | — | PB8 SCL / PB9 SDA，I2C1 AF6开漏，无内部上拉 |
| 编码器/按键 | — | PA0/PA1 TIM2 AF2，PA2低有效；保留消抖及短/长按 |
| 主时钟 | PH0/PH1，8.192MHz无源晶振 | HSI16内部16MHz |

封装脚位、IRQ与资源配置详见 [reference_pins.csv](reference_pins.csv) 和两端IOC。遥控USART1保持115200 8N1，OLED沿用HSI16内核及`0x00303D5B`时序，LoRa SPI1为4MHz。PA3电池与VREFINT交替转换，约10Hz更新，1/4 IIR滤波，错误/超时/陈旧值显示未知；不会用VDDA冒充电池或把电压档位描述成精确SOC。

## F411 时钟树

| 节点 | 最终值 |
|---|---|
| HSE | 8.192MHz，晶体模式 |
| PLL M/N/P/Q | 8 / 375 / 4 / 8 |
| PLL输入/VCO | 1.024MHz / 384MHz |
| SYSCLK/HCLK | 96MHz / 96MHz |
| PCLK1/PCLK2 | 48MHz / 96MHz |
| APB1定时器 | 96MHz；TIM5和TIM2分频95，计数1MHz |
| MCO1 | HSE/2 = 4.096MHz |
| SPI1 ADC / SPI2 LoRa | 3MHz / 3MHz |
| 内部ADC1 | PCLK2/4 = 24MHz，480周期采样 |
| SDIO | PLLQ=48MHz；识别400kHz；数据12MHz、四位 |
| USART1 | PCLK2=96MHz，115200 8N1 |
| 电压调节/Flash | Scale1 / 3等待周期 |

已核对 [ST RM0383](https://www.st.com/resource/en/reference_manual/dm00119316.pdf) 与 [F411数据手册](https://www.st.com/resource/en/datasheet/stm32f411ce.pdf) 的允许条件。初始化检查VOSRDY、HSE/PLL、总线频率及MCO设置后才启动ADC。运行时CSS/NMI立即把PA0切为GPIO低并锁定时钟/采样故障；禁止重新START、执行输出或NV写入，前台尽可能完成已有有效记录的收尾。

## ADS1220、速率及兼容策略

默认寄存器为 **`31 D4 40 00`**：AIN1-AIN2、gain1且PGA旁路、Turbo DR=110、连续转换、REFP0/REFN0外部5V参考，无温度/IDAC/烧断电流。AVDD=5V、DVDD=3.3V，激励与参考同源。外部CLK自动检测；没有虚构ID寄存器、专用RESET脚、双通道帧或ADC CRC。初始化执行RESET、默认寄存器读回、配置写入/读回及START，错误不会被视为就绪。

依据 [TI ADS1220数据手册](https://www.ti.com/lit/ds/symlink/ads1220.pdf)，标称2000SPS档在4.096MHz下稳定连续转换为2072个时钟，周期 **505.859375µs**，理论 **1976.833977SPS**。首转换保守等待114+2072时钟。约500ms窗口统计实际DRDY边沿，使用理论值±2%作就绪判断。实际板上DRDY频率和持续有效记录速率尚未测量，不能把理论值写成实测值；同源HSE也不能通过自身TIM5测出绝对晶振误差。

DRDY只启动3字节SPI DMA，队列保留32KiB，TF双缓冲各4KiB；不在DRDY中断中执行FatFs或复杂标定。丢失转换仅增加序号/缺失统计，不伪造记录。DRDY 10ms超时、DMA 1ms超时、SPI错误和异常间断触发采样故障。正常STOP仍等待排空、sync及close确认；源故障收尾后在安全空闲时POWERDOWN，重新运行需完整初始化。

STLOG001继续使用512B头、32B记录及记录CRC32，在保留区84..127增加`ADC1`扩展，描述型号、signed24、1个实际通道、输入、参考/激励、PGA/旁路、标称速率、转换周期及开始前测得的DRDY频率。旧第二通道槽填`INT32_MIN`表示不存在，CSV输出空值。导出器同时支持旧双通道日志/SZR1扩展，并区分实际DRDY、由序号推算的转换率和有效记录率。

输入电压换算为raw×5V/(2^23×gain)。未知传感器保留gain1旁路原始记录；高增益要求有效SENSOR.CFG并检查灵敏度、额定差分范围、参考、PGA共模余量。没有有效标定时不输出准确推力。

CAL0/CAL1双槽格式、CRC、generation及持久化提交次序保留，新增ADC型号/参考/输入/PGA绑定。旧ADC或配置不匹配的系数失效；迁移允许显式重新标定，写新槽成功后才提交，不先删除旧有效槽。永久零点/斜率、会话置零、自动置零配置及取消/保存流程保留，原始码不被修改。详见 [CALIBRATION.md](CALIBRATION.md) 和 [PROTOCOLS.md](../PROTOCOLS.md)。

## 完整离线验证结果

| 实际执行入口/项目 | 结果 |
|---|---|
| `Models/run_host_tests.py` | PASS，63组、60,110个C断言；新旧日志Python导出测试通过 |
| `Models/run_host_tests.py --english` | PASS，63组、60,110个C断言；英文UI及导出测试通过 |
| `Models/check_ads1220_capture.py`（主机入口也调用） | PASS，189断言；实际HAL/采集代码、3字节DMA、间断/时间戳、CS、abort、STOP代次与CSS丢弃 |
| `Models/check_reference.py` | PASS，6组、76,678检查；256撕裂写入、97压缩断电点、128重启/计数周期 |
| `Models/check_link_runtime.py` | PASS，12组、931断言；R8运行状态、重试及停止行为 |
| `Models/check_ignition_f4.py` | PASS，50检查；96MHz定时分频、自主截止、NMI永久封锁及抢占后恢复代码路径 |
| `Models/check_clock_fault.py` | PASS，13断言；提取实际板级CSS/NMI回调编译，检查故障标志、输出关闭、ADC屏障及错误仅计一次 |
| `Models/validate_candidates.py` | PASS，26测试；引脚、DMA、模拟输入及错误候选拒绝 |
| `Models/check_hardware_contract.py --source-dir <下载目录>` | PASS，8测试；来源SHA及实际重新提取一致、接线、重映射、PLL合法性和错误时钟配置拒绝 |
| `Models/check_ui_battery.py` | PASS，中英文及电池电路配置共8组合、84,288断言 |
| 两端原生CubeMX检查 | PASS，退出码0，Pinout与Clock均`Error:false` |
| `Models/build_reference.py --artifacts` | PASS，两个真实ReferenceApp EIDE目标；155份SDK输入哈希一致、无未定义符号、实际IRQ/NMI为强符号、所有加载段避开NV |
| `git diff --check` | PASS，无差异空白错误 |

日志已保存于 `firmware_host_validation*.log`、`reference_host_validation.log`、`link_runtime_validation.log`、`ignition_pulse_validation.log`、`ui_battery_validation.log`、`cubemx_r8_*.log` 和 `reference_arm_validation.log`。模型编译使用严格警告选项；没有删除断言或用固定成功结果代替实际执行。

本轮没有遗留未通过的离线测试，也没有因缺少工具跳过目标编译。CubeMX启动时第三方PDSC版本提示不影响最终两个目标的Pinout/Clock检查。实物测试未执行，结果单独列于下一节。

## 编译产物与 NV 边界

| ReferenceApp | text | data | bss |
|---|---:|---:|---:|
| Acquisition | 73,968B | 124B | 57,648B |
| Controller | 62,552B | 92B | 10,240B |

本地输出为 `dist/Acquisition/Acquisition_ReferenceApp.elf/.hex` 和 `dist/Controller/Controller_ReferenceApp.elf/.hex`，产物大小/SHA256见 `dist/manifest.json`。它们按仓库既有规则不纳入Git，未生成跨NV空洞的BIN。

F411 sector2/3（`0x08008000..0x0800FFFF`）及G071 page62/63（`0x0801F000..0x0801FFFF`）保持原边界，加载段检查均通过；配对信息、计数连续性及Flash日志含义未改变。本次未执行擦除或刷写。

## 尚需实物验证

1. HSE启动、MCO 4.096MHz和SPI mode1/CS时序；实测DRDY、首转换时间及晶振绝对误差。
2. 传感器实际型号、灵敏度、共模/满量程、参考与激励稳定性、噪声及真实载荷标定。当前未绑定特定传感器，默认只保证原始采集配置策略。
3. TF持续记录率、最坏写入延迟、丢样统计、拔卡/卡故障及STOP后文件sync/close完整性。
4. G071物理22/23脚UART、两端电池电压误差、OLED外部上拉与刷新、编码器方向/机械消抖、LED极性和射频连接/重连。
5. 保留NV区域的实际升级及掉电恢复；模拟断电覆盖不替代真实Flash电源扰动验证。
6. PA0上电低、ARM/FIRE独立确认、1000ms自主截止、STOP/故障关闭、HSE丢失时关闭波形和MOSFET实际输出。模型通过不代表实物安全验证通过；本任务没有自动执行负载测试。

## Git 交付

在当前 `main` 分支提交并普通推送到 `origin/main`；不强制推送，不覆盖已有远端变更。最终提交SHA及远端核验结果记录在本次交付消息中，以免在提交内容内写入自身SHA。用户原有未跟踪附件和PCB交接目录未纳入本次提交。
