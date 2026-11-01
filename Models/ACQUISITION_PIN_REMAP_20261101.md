# 采集端 MCU 引脚迁移交付 — 2026-11-01

实际参与构建的采集端 `ReferenceApp` 已迁移到用户指定的新引脚。最终11-01 epro2的原理图与PCB焊盘网络84项检查全部匹配，CubeMX资源检查、主机回归和双端EIDE ARM构建均通过。本次没有连接实板、烧录或触发真实执行负载。

## 新旧 GPIO 对照

| 功能 | 原GPIO | 新GPIO | LQFP64脚号 / 配置 |
|---|---|---|---|
| ADC_SCK | PA5 | PB3 | 55，SPI1 AF5 |
| ADC_MISO | PA6 | PB4 | 56，SPI1 AF5 |
| ADC_MOSI | PA7 | PB5 | 57，SPI1 AF5 |
| ADC_CS | PA4 | PB6 | 58，推挽输出，先置高再初始化 |
| ADC_DRDY | PC4 / EXTI4 | PA15 / EXTI15_10 | 50，下降沿，优先级0 |
| LED_V1 | PB6 | PA4 | 20，高有效，上电低 |
| LED_V2 | PB7 | PA5 | 21，高有效，上电低 |
| LED_V3 | PB8 | PA6 | 22，高有效，上电低 |
| LED_V4 | PB9 | PA7 | 23，高有效，上电低 |
| TF_DETECT | PB5 | PA9 | 42，输入上拉，低表示插入 |
| DEBUG_TX | USART1 PA9 | USART2 PA2 | 16，AF7，115200 8N1 |
| DEBUG_RX | USART1 PA10 | USART2 PA3 | 17，AF7，无流控 |

PB3/PB4改为SPI1 AF5，PA15改为GPIO外部中断，PA13/PA14保留SWD。F411通过GPIO模式和AF选择释放JTAG/SWO占用，不套用F1的AFIO重映射方式。PA9不再被串口初始化覆盖，PB5不再作卡检测；PC4、PA10、PB7/PB8/PB9释放。4P HY2.0 UART连接器仍为1=3.3V、2=GND、3=MCU RX、4=MCU TX。

以下保持不变：LoRa PB12..PB15、PC6 BUSY、PC7 EXTI7、PB2 NRESET；SDIO PC8..PC12/PD2；PC0电池；PB10链路LED；PA0 TIM2_CH1；PA8 MCO1；PH0/PH1 HSE；SWD、NRST、BOOT0和电源/VCAP。

HSE8.192MHz、SYSCLK96MHz、PCLK1/2=48/96MHz、MCO4.096MHz、SPI1 Mode1/3MHz、SDIO四位12MHz及DMA分配均不变。USART2改由APB1 48MHz计算波特率。ADC RX/TX仍为DMA2 Stream0/5 Channel3，SDIO仍为DMA2 Stream3/6 Channel4。

## 实际修改文件

| 范围 | 文件及内容 |
|---|---|
| 运行板级代码 | `Acquisition/board/reference_main.c`：GPIO、SPI1、CS、LED、TF检测、EXTI15_10、USART2向量；初始化/源故障/NV暂停恢复/CSS全部DRDY路径统一迁移 |
| 共享串口适配 | `Acquisition/ports/st_debug_uart_hal.c/.h`：F411 USART2 PA2/PA3 AF7、APB1和IRQ14；G071分支保留USART1 PA9/PA10 AF1、IRQ3；迟到TX完成只在BUSY态更新，不能清除已锁定错误 |
| SDIO注释 | `Acquisition/ports/st_sdio_hal.h`：卡检测注释更新为PA9，驱动逻辑未变 |
| CubeMX | `Acquisition/SwiftThrust_Acquisition_Candidate/SwiftThrust_Acquisition_Candidate.ioc`、`configure_candidate.mx`、`ignition_candidate.mx`：新AF/EXTI/UART和引脚；修正旧脚本的PB0链路LED残留为既有PB10 |
| 配置表 | `Acquisition/pin_candidates.csv`、`Models/reference_pins.csv`、`Models/config_pending.json`：新引脚、设计目标来源及最终CAD匹配证据 |
| CAD提取/验证 | `Models/extract_epro_contract.py`增加显式PCB PAD_NET提取；`check_acquisition_pinmap.py`、设计目标JSON、旧观测快照、最终快照和比较结果；`check_hardware_contract.py`明确历史CAD与当前GPIO检查边界 |
| 回归与构建 | `Models/check_debug_uart_hal.py`、`test_debug_uart_hal.c`、`debug_uart_model/*.h`；`check_acquisition_board.py`、`test_acquisition_board.c`；`check_clock_fault.py`、`run_host_tests.py`、`validate_candidates.py`、`build_reference.py`及执行日志 |
| 文档 | 根/Acquisition/Models README、`REFERENCE_APPLICATION.md`、`LORA_PROTOCOL_R8.md`中的LED脚、旧报告历史标识及本报告 |

已检查 `Acquisition/include/st_bsp.h`：它是明确拒绝目标构建的历史独立接口，不含运行GPIO，故保持原样；实际修改位于ReferenceApp和HAL端口。`Controller/`源文件与IOC没有改动，共享UART的G071分支由双平台模型及完整目标编译覆盖。

R8协议、ADS1220算法、STLOG001、标定、滤波/电量逻辑、PA0授权和自主截止实现均未重构。NV边界保持F411 sector2/3与G071 page62/63。

## CubeMX 和测试结果

首先完成新候选IOC的原生CubeMX检查，再迁移运行代码。CubeMX 6.15.0完整配置脚本与保存后复验均退出0，Pinout & Configuration、Clock Configuration均为`Error:false`，保存结果匹配CSV；官方器件/AF数据库同时确认F411RCT6 LQFP64、SPI1 AF5与USART2 AF7。PA15 EXTI15与PC7 EXTI7无冲突，SWD仍是Serial_Wire。

| 实际执行 | 结果 |
|---|---|
| `Models/cubemx_cli.py Acquisition/configure_candidate.mx ...` | PASS；完整配置命令、保存和引脚/时钟检查 |
| `Models/cubemx_cli.py Acquisition/revalidate_candidate.mx ...` | PASS；独立加载/保存复验 |
| `Models/validate_candidates.py` | 28测试通过，含错误旧映射、错误AF、EXTI边沿、DMA冲突及错误波特率拒绝 |
| `Models/run_host_tests.py` | 63组、60,110个C断言通过；Python新旧日志导出通过 |
| `Models/run_host_tests.py --english` | 同样63组、60,110断言通过，英文UI与导出通过 |
| 实际UART HAL与板级IRQ模型（已纳入上述两个入口） | F411 USART2与G071 USART1各661断言通过，覆盖初始化、TX/RX、缓冲溢出/回绕、错误及迟到完成 |
| 实际采集板函数模型（同上） | 137断言通过，覆盖SPI/GPIO/TF/LED/PA15路由、NV暂停恢复、CSS禁止恢复 |
| 实际ADS1220 HAL/采集模型（同上） | 189断言通过，DMA/时间戳/间断/STOP/CSS屏障保持 |
| 实际CSS/NMI回调模型（同上） | 13断言通过 |
| `Models/check_reference.py` | 6组、76,678检查通过，含256撕裂写入、97压缩中断点、128重启/计数循环 |
| `Models/check_link_runtime.py` | 12组、931断言通过，R8重试/配对/STOP及错误路径 |
| `Models/check_ignition_f4.py` | 50检查通过，PA0默认低、自主截止、故障锁及NMI抢占保护 |
| `Models/check_hardware_contract.py --source-dir <下载目录>` | 8测试通过；旧10-29快照原文/SHA保持，时钟与CSS条件保持 |
| `Models/check_acquisition_pinmap.py` | 10测试通过，包括新表一致性、原理图/PCB双域检查、旧CAD拒绝、释放脚、连接器与最终SHA |
| `Models/check_acquisition_pinmap.py --source <11-01 epro2> ...` | MATCH，84/84项，退出0 |
| `Models/build_reference.py --artifacts` | 双端真实EIDE ReferenceApp ARM链接通过，155份SDK输入SHA一致，无未定义符号，加载段避开NV |

ARM检查要求采集端`EXTI15_10_IRQHandler`、`USART2_IRQHandler`为实际强符号，并拒绝旧`EXTI4_IRQHandler`、`USART1_IRQHandler`强实现；遥控端继续检查USART1。模型和所有回归没有删除断言或跳过失败检查。

本轮无未通过的最终离线检查。开发中发现的CubeMX旧脚本冗余`project path`命令已修正并完整重跑通过。环境缺失的SDK缓存已按既有manifest恢复并校验，未升级库版本。日志见 `cubemx_acquisition*_20261101.log`、`firmware_host_validation*.log`、`reference_host_validation.log`、`link_runtime_validation.log`、`ignition_pulse_validation.log`、`reference_arm_validation.log`。

| 目标 | text | data | bss |
|---|---:|---:|---:|
| Acquisition | 73,984B | 124B | 57,648B |
| Controller | 62,568B | 92B | 10,240B |

本地稀疏HEX/ELF：`dist/Acquisition/Acquisition_ReferenceApp.*`、`dist/Controller/Controller_ReferenceApp.*`，产物大小及SHA见`dist/manifest.json`；按仓库规则不纳入Git。未生成跨NV空洞的BIN。

## 最终 epro2 一致性

最终读取文件为 `ProDoc_Acquisition_1_2026-11-01.epro2`，SHA256：

`839d88fe635155fbf0e7a4a1c8594dda41facc43cd6e28ba2d72c761692ddfb4`

[最终快照](hardware_acquisition_20261101_final.json) 和 [84项比较结果](acquisition_pinmap_cad_comparison_20261101.json) 同时记录原理图连接及PCB显式焊盘网络。33个活动信号、5个释放脚、4个串口连接器针脚分别在两个域核对，全部匹配。未把图形交叉视作短接，也未把空网络的焊盘互相连接。

`hardware_*_20261029.json`原样保留。最初读取的10-31工程SHA为`8d87e4746a4a5b9685725504f523048d1c0ec119e856c391fa2fd012c1e14e60`，它仍是旧接线，故另存`hardware_acquisition_20261031_observed.json`，未把该旧文件冒充最终工程。用户随后确认更新，实际新导出文件日期为11-01，最终验证依据上述新文件。

CAD匹配是电气网络分配一致性检查，不是铜皮连通性/DRC或实物验证。尚需上板验证SWD、USART2针序/收发、SPI1与DRDY波形、TF检测和连续写入、LED极性、LoRa及PA0安全波形；本次没有连接或触发负载。

## Git 交付

提交并普通推送到新分支 `codex/acquisition-pin-remap-20261101`，不覆盖或强制推送`main`。最终提交SHA和远端核验结果写入交付消息；原有未跟踪附件及PCB交接目录不纳入本次提交。
