# 2026-11-08 遥控端编码器 A 迁至 PA5

按用户要求修改实际 EIDE ReferenceApp 运行固件：`Controller/board/reference_main.c` 的 `HAL_TIM_Encoder_MspInit` 将 GPIOA 的引脚掩码从 PA0/PA1 改为 PA5/PA1。只改变编码器 A 的 GPIO，PA0不再由编码器初始化；没有更改采集端、PCB、协议或其他功能。

## 引脚及保留行为

| 功能 | 原引脚 / LQFP32脚号 | 新引脚 / LQFP32脚号 | 配置 |
|---|---|---|---|
| 编码器 A | PA0 / 7 | PA5 / 12 | TIM2_CH1，AF2，上拉 |
| 编码器 B | PA1 / 8 | PA1 / 8 | TIM2_CH2，AF2，上拉 |
| 编码器按键 | PA2 / 9 | PA2 / 9 | 低有效上拉，30ms去抖、800ms长按 |

运行代码继续采用 TIM2 硬件 TI12 编码器模式，Prescaler=0、Period=65535、双通道上升沿/直接输入/不分频、IC1Filter=IC2Filter=8。计数回绕、档位换算、方向与按键处理逻辑未改。

依据 [ST STM32G071x8/xB 数据手册](https://www.st.com/resource/en/datasheet/stm32g071kb.pdf) 的封装/复用表，以及本机 CubeMX 6.15.0 官方 `STM32G071K(6-8-B)Tx.xml`、`GPIO-STM32G07x_gpio_v1_0_Modes.xml`，PA5第12脚支持TIM2_CH1 AF2，PA1第8脚支持TIM2_CH2 AF2。候选表及ReferenceApp引脚表均检查精确LQFP32封装和重复资源。

PA5原来空闲；LoRa仍为PB3/PB4/PB5 SPI1、PA15 CS、PB7 BUSY、PB6 DIO1、PA8 NRESET；OLED仍为PB8/PB9；电池ADC仍为PA3；USART1逻辑PA9/PA10及物理22/23脚重映射、SWD PA13/PA14、PB0链路LED保持。未启用PA5的SPI1/USART3/ADC/DAC等其他复用。

2026-10-29遥控端CAD快照保留历史事实，本次PA5分配依据用户要求，不把旧快照改写成新PCB证据。未修改或验证新的遥控端PCB文件。

## 配置与测试更新

- `Controller/SwiftThrust_Controller_Candidate/*.ioc`、`configure_candidate.mx`和`revalidate_candidate.mx`：迁移PA5并释放PA0；将候选配置同步到运行代码已有的TI12、滤波8和65535计数周期。旧IOC缺少这些参数，原生默认值与实际运行配置不同。
- `Controller/pin_candidates.csv`、`Models/reference_pins.csv`及当前文档：更新PA5、第12脚和PA0释放说明。
- `Models/check_controller_encoder.py`：提取并执行真实MSP和GPIO函数，检查AF/上拉、精确引脚掩码、PA0释放及非TIM2句柄不产生配置；纳入中文/英文主机回归。
- `Models/validate_candidates.py`：增加G071官方AF表校验、两个引脚表一致性，以及旧PA0映射/错误复用/滤波/计数模式的拒绝测试。
- `Models/cubemx_cli.py`及遥控端脚本：核对CubeMX实际查询的TIM2和ADC参数，避免只依据进程返回0判断配置成功。

CubeMX 6.15.0保存时省略了ADC的`ClockPrescaler`和`ScanConvMode`有效默认值。独立重新加载后实际查询仍为`ADC_CLOCK_SYNC_PCLK_DIV2`和`ADC_SCAN_DISABLE`；16MHz时钟树、PA3及长采样周期保持。离线检查允许这一保存形式，同时继续拒绝显式错误分频、扫描模式或总线时钟；原生CLI要求查询值完全一致。未修改运行ADC代码。配置脚本移除了旧的多余`project path`命令；迁移时先解锁PA0，再将CH1分配给PA5，由CubeMX释放旧脚。

## 实际验证

| 检查 | 结果 |
|---|---|
| CubeMX完整配置及独立重新加载/保存 | 通过；Pinout & Configuration / Clock Configuration均Error:false；PA5=TIM2_CH1、PA1=TIM2_CH2、PA0=Reset_State |
| CubeMX实时参数查询 | TI12、Prescaler=0、Period=65535、IC1/IC2Filter=8，ADC分频与单次模式保持 |
| `validate_candidates.py` | 30测试通过，包含原28项和新增遥控端正/负向检查 |
| 实际编码器MSP/GPIO模型 | 11检查通过；主机HAL调用模型，不是实物波形测试 |
| `run_host_tests.py`及`--english` | 各63组、60,110主套件断言通过；另含新的MSP模型、双端UART及既有板级模型 |
| 既有编码器/按键回归 | 计数回绕、档位、按键抖动、上电按住抑制及单次事件均通过 |
| `check_reference.py` | 6组、76,678检查通过，配对/集成/NV保护保持 |
| `check_link_runtime.py` | 12组、931断言通过，R8与STOP流程保持 |
| `build_reference.py --artifacts` | 双端实际EIDE ReferenceApp ARM编译/链接通过；155份SDK哈希匹配，所需强符号保留、无未定义符号、加载段避开NV |

遥控端text/data/bss仍为62,568 / 92 / 10,240字节。采集端源码未变，交叉编译作为对照；其原厂ST Flash HAL仍有3条未使用参数警告。CubeMX有未使用扩展包PDSC环境告警，最终命令/资源/时钟检查通过。

证据：`cubemx_controller_configure_20261108.log`、`cubemx_controller_20261108.log`、`controller_encoder_validation_20261108.log`、`firmware_host_validation*.log`、`reference_host_validation.log`、`link_runtime_validation.log`、`reference_arm_validation.log`。本地稀疏HEX/ELF位于`dist/`，不提交构建产物。

完成验证后按用户授权直接提交并普通推送到GitHub `main`，不创建PR、不强制推送。未连接实板、探针或串口；实物编码器方向、机械触点和波形仍需上板核对。
