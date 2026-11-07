# 2026-11-07 完成 PCB 与 main 合并前复核

结论：未发现阻止采集端引脚迁移合并的固件问题。11-07 完成版工程的 MCU 分配与已实现的 ReferenceApp 一致，双端离线回归和 ARM 编译通过。用户已授权合并并推送 main，允许外部连接器线序调整。本轮没有新增固件行为，新增 CAD 证据、检查器回归、接线说明和本报告。

审查基线：`origin/main` 为 `167c5a057e67c8b19f61944d69746ac9ca1910e3`；迁移分支为 `codex/acquisition-pin-remap-20261101`，固件提交为 `d82439fee3b077c46d96947b6e06183ccf521c41`。获取远端后，迁移分支相对 main 为 ahead 1 / behind 0。完成证据提交后使用普通合并、普通推送，不强制推送。

## CAD 文件和引脚

- 来源：用户提供的 `ProDoc_Acquisition_1_2026-11-07.epro2`，479,443 字节。
- 原文件 SHA256：`610b23aa5d4bedaa2fcd6038ca4c10a1d99fc2341033024915da307145c79be2`。
- [独立快照](hardware_acquisition_20261107_final.json)、[逐项比较结果](acquisition_pinmap_cad_comparison_20261107.json)。11-01 及更早硬件快照不覆盖。
- 原理图、PCB 显式 PAD_NET 分别检查 33 个活动信号、5 个释放脚和4个串口针脚，共 **84/84 匹配**。
- 扩展检查所有同时具有原理图命名网络和 PCB 焊盘的条目，**217/217 网络标签一致**；这不等于所有铜连通或 DRC 通过。

| 采集端资源 | 复核结果 |
|---|---|
| SPI1 | PB3/PB4/PB5 AF5，PB6 CS；Mode 1、3MHz、DMA2 S0/S5 C3 保持 |
| DRDY | PA15 下降沿 EXTI15_10，优先级0；PC7 LoRa EXTI7、优先级4保持 |
| 电量 LED | PA4～PA7，高电平点亮；只迁移 GPIO |
| TF | PA9 检测，低有效上拉；PC8～PC12/PD2 SDIO 及 DMA 不变 |
| 调试串口 | USART2 PA2/PA3 AF7，APB1 时钟，115200 8N1，USART2 IRQ |
| SWD | PA13/PA14 保持，PB3/PB4 不再使用 JTAG/SWO，PA15 不再使用 JTDI |
| 时钟和输出 | PH0/PH1 HSE 8.192MHz、PA8 MCO1 4.096MHz、PA0 TIM2_CH1 保持 |

原理图实物器件的名称/命名网络与11-01版本对比，变化集中在 `SENSOR_IN1/2` 的外部针序；器件型号未改变。匿名电源符号的内部 ID 变化不当作电气变化。

| SENSOR_IN1 和 SENSOR_IN2 脚号 | 11-01 | 11-07 原理图及 PCB |
|---|---|---|
| 1 | 5V | A− |
| 2 | GND | A+ |
| 3 | A+ | 5V |
| 4 | A− | GND |

用户明确允许此变更。按新版制作/核对线束即可，不需调整 MCU 或 ADC 代码。UART 外部针序仍为1=3V3、2=GND、3=MCU RX、4=MCU TX。

## 固件审查与验证

审查了实际参与 ReferenceApp 构建的 `Acquisition/board/reference_main.c` 和共享 UART HAL 的差异及调用路径。DRDY 初始化、ISR/回调、标志清除、采样超时、NV 暂停/恢复和 CSS/NMI 禁止恢复均指向新资源。旧 USART1 不会覆盖 PA9；共享驱动的 G071 分支仍使用 USART1。迟到 TX 完成回调不会清除已锁定的 UART 错误。

`Controller/` 源码与 IOC 没有迁移改动。R8、ADS1220 算法、日志、标定、PA0 输出授权/自主截止、NV 分区及 TF 停止收尾保持。没有连接探针、串口、实板或真实执行负载。

| 2026-11-07 实际执行 | 结果 |
|---|---|
| CubeMX 6.15.0 加载、资源/时钟检查、保存复验 | PASS；Pinout & Configuration 和 Clock Configuration 均 Error:false，保存 IOC 匹配计划 |
| `validate_candidates.py` | 28 测试通过，包含 AF/EXTI/DMA/时钟及旧映射拒绝 |
| `run_host_tests.py`，及 `--english` | 各63组、60,110个主 C 套件断言通过，日志导出通过 |
| 实际 ADS1220 HAL/采集模型（上面两个入口内） | 各189断言通过 |
| 实际 UART HAL/板级 IRQ 模型（同上） | F411 USART2 和 G071 USART1 各661断言通过 |
| 实际板级 GPIO/SPI/EXTI/NV 模型（同上） | 各137断言通过 |
| 实际 CSS/NMI 模型（同上） | 各13断言通过 |
| `check_reference.py` | 6组、76,678检查通过；覆盖256撕裂写入、97压缩中断点、128重启/计数循环 |
| `check_link_runtime.py` | 12组、931断言通过，含 R8 与 STOP 路径 |
| `check_ignition_f4.py` | 50检查通过；默认低、自主截止、故障锁及 NMI 保护 |
| `check_hardware_contract.py --source-dir <下载目录>` | 8测试通过；历史 CAD/时钟契约保持 |
| `check_acquisition_pinmap.py` | 12测试通过；保留原10项，新增11-07证据/比较报告及连接器/217焊盘检查 |
| `check_acquisition_pinmap.py --source <11-07 epro2> ...` | 84/84 MATCH，退出0 |
| `build_reference.py --artifacts` | 两端真实 EIDE ReferenceApp ARM 编译/链接通过；155/155 SDK 文件 SHA 一致 |
| ELF/HEX 检查 | 实际 ISR 强符号保留，无未定义符号；加载段避开两端 NV；4个产物大小/SHA 与 manifest 一致 |
| `git diff --check` | PASS |

证据：[CubeMX 日志](cubemx_acquisition_20261107.log)、[引脚/资源/历史契约回归](pinmap_validation_20261107.log)、[中文主机](firmware_host_validation.log)、[英文主机](firmware_host_validation_en.log)、[集成](reference_host_validation.log)、[链路](link_runtime_validation.log)、[输出驱动](ignition_pulse_validation.log)、[ARM](reference_arm_validation.log)。CubeMX 环境产生更新器/未使用扩展包告警，资源和时钟检查仍通过；ARM 编译有3条 ST 原厂 Flash HAL 未使用参数警告，无项目代码编译错误。未将远端 CI 状态声称为通过。

| ReferenceApp | text | data | bss |
|---|---:|---:|---:|
| Acquisition | 73,984B | 124B | 57,648B |
| Controller | 62,568B | 92B | 10,240B |

本地 `dist/` 保留稀疏 HEX/ELF 和 manifest，按仓库规则不提交产物。新增内容仅为证据、测试和文档，不改变刚完成编译的 C 源码。

## PCB 建议及证据边界

读取了最新存储票据中的 PCB/封装几何及工程自带预览。板框约85×75mm；配置的铜层顺序为 Top / GND / SIG / Bottom。文件包含94个PCB器件、477段LINE、89个独立VIA、11个POUR，并有存储的铺铜数据；这些数量不证明已布通或通过制造检查。

1. **优先补充局部 GND 过孔。** 89个独立VIA均没有分配到GND，现有通孔连接器可以连接地层，但不能据此确认SMD去耦有足够短的回流。建议在MCU去耦、ADS1220的C22/C23/C24地端、ADC的DGND/AVSS附近检查并布置就近接地过孔；同时检查信号换层处的返回路径。不要因为划分模拟/数字区域而切断连续地平面。[ADS1220 数据手册 §9.3.3、§9.4](https://www.ti.com/lit/ds/symlink/ads1220.pdf)建议低阻抗去耦接地及连续地参考。这是从文件几何提出的改进建议，不是“地网悬空”的结论。
2. **复核 TPS61040 的开关回路。** 从封装焊盘中心计算，DCDC_5V4.SW 到 L2.1 直线约3.50mm、到D1.2约3.96mm，VIN到C4.2约3.00mm；这些是几何距离，不是走线长度或超标阈值。可进一步压缩芯片、L2、D1、C5的高频回路，并确认R5/R6反馈回路避开SW和电感。[TPS61040 数据手册 §7.5](https://www.ti.com/lit/ds/symlink/tps61040.pdf)明确要求输入电容、开关电感和二极管靠近相应引脚，反馈部分避开电感并有地屏蔽。功率铜皮的窄颈/热焊盘连接是否够用还需结合实际峰值电流、铜厚和压降；本次没有据网名推定额定电流。
3. **保留源端串阻，并检查最终地参考。** R16的MCU侧焊盘距PA8约1.93mm，ADC端MISO串阻R37距ADC输出焊盘约2.22mm，位置方向合理。ADC_CLK显式LINE总长约31.98mm，经过Top和SIG层、2个过孔；TF_CLK约34.64mm，同样换层。此统计仅为同名网LINE长度之和，未包括焊盘、过孔垂直长度和串阻另一侧网络。建议检查这些路径的地参考和附近模拟输入，保持回流连续；不因主频只有几MHz而忽略边沿。ADS1220手册§9.4建议数字走线尽量少穿越敏感模拟区，并保持就近数字回流。

制作前仍需在EasyEDA原生环境重铺铜，查看未布通网络与DRC，导出Gerber后复查四层层序、焊盘/钢网开口、极性和连接器机械方向。本轮没有原生ERC/DRC报告、完整铜连通求解、制造输出复核或实板测量；上述固件合并结论不替代这些PCB验收。
