> R8更新：本文保留硬件/构建/NV基础接入资料；旧菜单、握手及5秒输出描述以[R8完整协议](LORA_PROTOCOL_R8.md)为准。R8真实输出默认关闭，PB0新增链路灯，不能按旧描述直接启用执行级。

# SwiftThrust 双板参考应用

2026-10-20。本次交付是可编译的 `ReferenceApp` 双板应用；板子尚未完成，所有接线、极性和时钟均为明确可替换的参考设计。没有连接探针、串口或硬件，没有烧录和上板运行。旧 `LinkCheck` 目标只用于组件链接，不能作为运行固件。候选 IOC 保持原有 pinout 记录，不是本应用代码生成源；实际初始化在两板 `board/reference_main.c`。

> **上板前必须核对：PCB尚未冻结，本配置不是实物板测结论。** 最终引脚、CS/RESET/卡检测/LED极性、3.3V供电、外部8.192MHz ADC时钟、传感器额定值/输入共模/激励兼容性、TF和OLED接口必须与参考契约一致。程序会驱动参考CS/RESET/LED；不一致时应先改源码、重建，再由用户自行验证。2.5V只是标定配置的激励假设，未确认前保持raw-only。电池分压/充电电路未选定，显示仅MCU VDDA。自动烧录不执行。

## 参考条件与可替换参数

| 项目 | 当前参考配置 | 尚须实板确认 |
|---|---|---|
| 采集 MCU | STM32F411RCT6，LQFP64，256KiB Flash / 128KiB SRAM | 实物型号、3.3V 电源与复位 |
| 遥控 MCU | STM32G071KBT6，普通非 N，LQFP32，128KiB Flash / 32KiB SRAM | 实物型号与 BOOT/SWD 接线 |
| F411 时钟 | HSI16 / M16 × N336 / P4 → 84MHz；APB1 42MHz、定时器84MHz；PLLQ7 → SDIO48MHz | HSI 精度、温漂及 SDIO 稳定性；不是精密计量时基 |
| G071 时钟 | HSI16 直接 16MHz；I2C 内核 HSI16 | 总线实测波形 |
| ADS131M02 | 独立外部 8.192MHz CLKIN；高分辨率 PWR2、OSR2048、两路 PGA1、24bit、CCITT 输出CRC、SPI mode1 | 时钟、电源、输入绝对值/共模与实际模拟链路 |
| 采样 | 名义 2,000SPS，TIM5 1MHz 扩展64bit DRDY 时间戳；启动以约500ms边沿计数检查 ±2.5% | 逻辑分析仪核对 CLKIN/DRDY、采样连续性及绝对时间误差 |
| LoRa | 双端2473MHz / +12dBm / SF10 / BW812500 / CR4/5 / sync0x12 / 16符号前导 / 显式头 / PHY CRC / normal IQ；500ms TX超时；高灵敏度LNA、保留AGC | 两板实际模块配置、接收电流与距离；没有距离承诺 |
| OLED | SSD1306 128×64 I2C，7bit 0x3C，HSI16 下 timing=0x00303D5B，外部3.3V上拉 | 实际屏控型号/地址、上拉与上电复位；不混用 SH1106 |
| 编码器 | TIM2 四边沿/格；按钮低有效，30ms去抖 | 实际编码器每格边沿数和方向 |
| 电压 | 采集PC0/ADC1_IN10经10k+10k分压读18650；VREFINT校正VDDA；遥控SELF仍是自身VDDA | 分压、100nF、电芯/负载下误差；非精密SOC |
| 四灯 | F411 PB6..PB9 四颗绿色LED高有效，各串限流电阻；3.90/3.70/3.50V依次减灯，低于3.30V末灯1Hz闪，30mV回差，过期1s全灭 | LED顺序/极性、实际电芯曲线 |
| 点火逻辑信号 | PA0/AF1 TIM2_CH1，PWM2/OPM自主高5s后低，STOP提前低；启动默认低 | 逻辑波形、执行级输入规范；无导通/点火反馈 |

不猜测传感器型号、额定力、灵敏度或4/6线连接。默认无 `SENSOR.CFG` 时照常记录两路原始ADC，力值/标定功能不可用。PGA1是保守参考起点，不是最终灵敏度/噪声优化结论。未知电压不会伪装成0V或满电。参考配置中代码的 `verified/accepted` 表示采用本参考接线契约并通过运行时检查，**不表示已经上板验证**。

## 接线

完整封装位置见 `reference_pins.csv`；PA0已同步到候选表和IOC。公共逻辑电平3.3V并共地；ADC模拟布线/去耦按器件数据手册实施。

| 采集板 F411 | 连接 |
|---|---|
| PA5 / PA6 / PA7，AF5 | ADS SCLK / DOUT / DIN，SPI1 mode1，2.625MHz |
| PA4 / PC5 / PC4 | ADS CS低有效 / SYNC-RESET低有效 / DRDY下降沿；CLKIN接外部8.192MHz振荡源 |
| PB13 / PB14 / PB15，AF5 | E28 SCK / MISO / MOSI，SPI2 mode0，2.625MHz |
| PB12 / PC6 / PC7 / PB2 | E28 CS / BUSY / DIO1上升沿 / NRESET低有效 |
| PC8..PC11 / PC12 / PD2，AF12 | TF DAT0..3 / CLK / CMD，四位SDIO，工作时钟12MHz；按SD接口要求外接上拉 |
| PB5 | TF卡检测，插卡触点接地，内部上拉，低表示在位；无检测线时须修改 `present()`，不能悬空代替 |
| PB6 / PB7 / PB8 / PB9 | 电压灯1..4，高有效；这些引脚在本参考程序中会输出 |
| PC0 / ADC1_IN10，物理8脚 | 开关后的BAT+经10k到PC0，PC0经10k及100nF到GND；4.2V电池分成约2.1V，不能直接接电池 |
| PA0 / AF1 TIM2_CH1，物理14脚 | 外部执行级3.3V兼容高有效逻辑输入，输入端约10k下拉，信号地与采集板共地；不直接驱动点火头/线圈 |
| PA9 / PA10，AF7 | USART1 TX / RX，115200 8N1，仅调试与本地配对 |
| PA13 / PA14 | SWDIO / SWCLK，独立于调试UART |

| 遥控板 G071 | 连接 |
|---|---|
| PB3 / PB4 / PB5，AF0 | E28 SCK / MISO / MOSI，SPI1 mode0，4MHz |
| PA15 / PB7 / PB6 / PA8 | E28 CS / BUSY / DIO1上升沿 / NRESET低有效 |
| PB8 / PB9，AF6 | OLED SCL / SDA，外部上拉到3.3V |
| PA0 / PA1，AF2；PA2 | 编码器A/B；短按按钮接地，内部上拉 |
| PA9 / PA10，AF1 | USART1 TX / RX，**物理19/21脚**，没有重映射到PA11/12；已按ST G071示例设置dead-battery strobe |
| PA13 / PA14-BOOT0 | SWDIO / SWCLK，物理24/25脚 |

点火已接通R7的ARM/FIRE/DISARM和PA0自主5秒脉冲；只证明MCU控制流程，不能确认外部执行级存在、关闭或点着。PA11/PA12仍未配置。两端内置项目校验，无须日常输入密钥或抄UID；自动发现及握手见[完整报文流程](LORA_PROTOCOL.md)。

## 首次配对与操作

这部分是用户以后自行上板时的说明，本轮没有执行。无需为原始采集填写传感器参数。

1. 按参考接线为一对双板上电，先不连接执行级能量负载。空白设备自动发现、持久记住UID，再自动HELLO/COMMIT，无须串口输入；已有历史只连接原对端，损坏历史不会自动覆盖。
2. 等待LINK OK；原射频调制/同步/分帧参数需一致。需要诊断时用115200 8N1、3.3V USB-UART，TX/RX交叉并共地；INFO和STATUS能读取UID/NV及电压。旧PAIR/NVINIT保留为本地维护，不是日常流程。
3. CAPTURE ONLY的START确认后记录，STOP等终态收尾；上电/重连不自动重放采集。IGNITION ONLY的START确认申请准备，再在独立FIRE确认页旋转到CONFIRM FIRE并按下；5秒后MCU信号低。STOP/BACK可提前撤销。
4. IGNITION + CAPTURE先开始并确认记录，再准备/确认点火；脉冲结束继续记录，STOP/BACK收尾。准备有效10s，过期须重新准备；点火回复丢失只查询状态，不自动补发FIRE。
5. 采集板卡需预先由电脑格式化成当前FatFs支持的FAT/FAT32；不使用exFAT，不自动格式化。日志独占新建 `S0000000.BIN` 递增，已有文件绝不覆盖；同名只在后续循环继续寻找。数据是STLOG001，结束写END、sync和close后才报告终态完成。
6. STOP后再取卡。掉电或拔卡不会保证最后一段完整；`python -B Models/export_stlog.py <日志> <新CSV>` 可以检查CRC并恢复有效前缀。原始值始终保留，未校准时力值列为空。

UART保留INFO、STATUS、PAIR、NVINIT NEW及IGNKEY临时维护入口，不接受本地START/STOP/FIRE。IGNKEY只改RAM并退休会话，正常上电使用内置项目校验；无需操作它。NVINIT NEW不是故障格式化/清计数指令。无卡可自动连接和仅点火，采集START仍要求卡和ADC就绪。

标定：将 `Models/SENSOR.CFG.example` 的字段按真实传感器/测量噪声填写，显式ACK后复制为TF根目录 `SENSOR.CFG` 并重启。模板的0值故意使标定无效。激励2.5V必须适合实际传感器，配置并不产生或测量激励电源。进入采集页的CALIBRATION，依次卸载归零、输入已知载荷、加载统计、核对后SAVE；原有CAL0/CAL1双文件只在新值完成sync/封印/close/readback后更换。更换传感器或ADC配置必须改变绑定并重新标定；不能套用旧系数。CH1的真实电气缩放未绑定时不应解释成已验证的激励监测电压。详见CALIBRATION.md。

## 停止采集与待确认操作

在 CAPTURE ONLY 页旋转编码器选中 STOP，再按下编码器；没有独立停止按钮，也无需第二次确认。START 尚在等待回执时，STOP 先记录为 `STOP QUEUED`。当前无线发送结束且状态新鲜后，遥控器发送序号更大的 STOP；不等待 START 回执，也不重放 START。即使 START 回执迟到、丢失，或 STOP 比旧 START 先到，采集端也按命令序号拒绝后到的旧 START。

`WAIT STOP CONFIRM` 表示正在等待停止完成。采集端先禁止新样本入队，然后排空缓存、写结束记录、同步并关闭 TF 文件；只有匹配 STOP 的完成回执才显示 `STATE CONFIRMED`。配合新鲜的 `ACTUAL IDLE` 确认收尾后，再进行下一次 START。仅收到“已接受”或 IDLE 状态广播，都不能结束待确认的 STOP。

重复按 STOP 不产生新命令、不重置重试次数，也不延长本轮时限。沿用每轮最多4次、500ms ACK 等待加抖动和120s总时限；排队STOP从提出意图起计算总时限。状态过期时可保留待确认 START 的停止意图，但恢复新鲜状态前不发送 STOP。`LINK LOST / STOP ?`、`STOP UNCONFIRMED` 或 `STOP FAILED / CHECK` 都不代表已停止；不允许重新 START。链路恢复后显式再按 STOP，会按原 STOP 身份重试；或通过 STATUS→CONNECT 显式重连。重连成功取消本地旧意图，采集端仍须按既有规则先停止收尾才能建立新会话。没有自动重连或自动重新 START。

其他命令忙碌显示 `BUSY / WAIT COMMAND`，链路无效显示 `LINK LOST / CONNECT`，状态不允许显示 `STATE UNAVAILABLE`，序号用尽显示 `RECONNECT REQUIRED`；`NOT IMPLEMENTED` 保留给真正未实现的静态页面。普通无线信号中断或遥控器关机不会自动停止采集，不能代替 STOP。此次应同时升级双板 ReferenceApp，硬件与引脚配置不变。验证说明见 [STOP_ORDER_REVIEW](STOP_ORDER_REVIEW.md)。

## 调度与故障

F411 DRDY与SPI1 DMA IRQ同抢占级0，互不嵌套；ISR只搬12字节帧/时间戳/队列，不做CRC、FatFs或打印。TIM5溢出扩展的读取/更新短临界区保证一致。主循环是同一个串行控制/卷owner：logger服务、calibration步骤、radio/server步骤、FatFs worker。SD每个512B扇区等待，以及射频BUSY/reset等待，会调用有重入保护的fast_service服务logger和电压/UART，再WFI让中断推进；不递归调用FatFs、校准存储或无线状态机。实际SDIO命令和卡最坏延迟仍需测量；40KiB队列/双缓冲不是任意慢卡的保证。

ADC初始化必须拿到ID、寄存器写ACK/读回和LOCK响应才能交给捕获。无有效帧超过10ms或速率失配会停止并排空，质量标志可见；TF移除/短写/IO错误锁定FAULT，不自动重挂载或覆盖重启。射频驱动锁定故障也停止并尽量收尾，采集板需检查后复位。遥控射频故障可通过显式CONNECT重建射频owner，OLED总线故障显示在UART并需检查后复位。单纯遥控信号暂时缺失不会自动开始新事务。

已配对控制器发起**新连接**而采集正在进行时，采集板先STOP/排空，再等HELLO重试提交NV；未完成排空会拒绝本次HELLO，必要时用户再次CONNECT。新连接会取消未保存的校准草稿，保留已保存标定。采集期间不擦写NV。

## NV与升级边界

| MCU | 两个物理NV擦除区 | 应用布局 |
|---|---|---|
| F411RCT6 | sector2 `0x08008000..0x0800BFFF`，sector3 `0x0800C000..0x0800FFFF` | 向量在0x08000000；代码/只读/数据加载从0x08010000起，192KiB容量 |
| G071KBT6 | page62 `0x0801F000..0x0801F7FF`，page63 `0x0801F800..0x0801FFFF` | 应用前124KiB |

后端检查实物Flash容量。每个256B日志帧保存两个96B协议槽、递增64bit序号、CRC和最后写入的commit封印；写入/readback完成后才返回成功。填满当前区后只在空闲且VDDA有效时擦另一整区，提交新快照后才使用它。Flash写/擦期间CPU取指可能暂停，因此禁止在采集中进行；首次使用不依赖外置FRAM。未完成帧、坏CRC、身份不符或不连续历史会拒绝工作，不静默回滚到旧计数。

**升级必须保留上述两个区。** `ReferenceApp` 的链接脚本和构建检查都排除了NV；`dist`只交付稀疏HEX和ELF，没有跨越NV空洞的BIN。程序员仍必须选择仅擦应用占用扇区/页，不能整片擦除或改Option Bytes。EIDE自动烧录入口故意拒绝；本轮不会连接烧录器。旧LinkCheck布局不保留这些区域，不得用于升级已配对设备。

断电落在日志写/擦窗口可能使NV锁定，需要另行人工维修并重新分配**从未使用过的网络号/epoch范围**；不能清零后继续沿用旧范围。更换配对、恢复损坏历史和整片擦除不是本UART接口提供的操作。CRC与公开UID不构成对恶意发射者的密码认证，本协议只用于本轮普通遥控采集。

## 构建与离线证据

打开根目录 `SwiftThrust.code-workspace`，两EIDE工程均选择 `ReferenceApp`。`LinkCheck` 保留为历史组件目标。SDK_ROOT/ARM_GCC_ROOT使用现有本地环境；没有复制完整SDK、下载依赖、增加RTOS或构建副本。

```powershell
python -B Models/build_reference.py --artifacts
python -B Models/run_host_tests.py
python -B Models/check_link_runtime.py
python -B Models/check_reference.py
```

构建脚本直接读取实际EIDE清单、运行已安装unify_builder，临时对象自动删除；`--artifacts`仅保留dist/Acquisition和dist/Controller各自稀疏HEX/ELF。构建会检查无未定义符号、实际main/MSP/IRQ为强定义、所有加载区不覆盖NV；日志带本地源码SHA256。SDK清单为official_header_manifest.json，严格校验同一份官方输入。

离线通过：完整50组/54,398个C断言与Python日志导出检查；含真实未修改FatFs在合成卡上持续2kHz模型与sector-wait协作服务3000条记录及收尾。无线owner 7组/383断言。参考参数/配对/Flash模型5组，含128次重启计数、256个写入中断点和97个整理中断点。详见firmware_host_validation.log、link_runtime_validation.log、reference_host_validation.log、reference_arm_validation.log。上述都不是硬件断电、射频链路或持续上板测试。

用户以后上板的最小核对项：实际接线/电平与型号；CLKIN/DRDY/SPI/DMA及TIM5时间戳；TF写入/STOP/拔卡与掉电恢复；双板配对/重启/丢包/重连期间不自动START；OLED方向/编码器格数/VDDA与四灯；真实载荷标定与不同卡的最坏写延迟。改变引脚/时钟/ADC增益/电压量程时先修改参考源码和绑定，再重新构建。尚无固件或操作系统级升级器。

来源：ADS131M02寄存器、时钟/电气范围以 [TI datasheet](https://www.ti.com/lit/ds/symlink/ads131m02.pdf) 为依据。G071 dead-battery strobe参照 [ST G071官方MSP示例](https://raw.githubusercontent.com/STMicroelectronics/STM32CubeG0/master/Projects/NUCLEO-G071RB/Examples/I2C/I2C_TwoBoards_ComIT/Src/stm32g0xx_hal_msp.c)。封装引脚与AF使用本地ST CubeMX数据库；VREFINT校准地址/电压取官方LL ADC头文件，Flash几何取目标HAL/CMSIS定义。参考I2C timing在本配置下选HSI16，仍需测量实际总线时序。

首次START准备先确认时钟/epoch，再调用真实标定回调，因此从TF恢复的既有系数首条日志即生效。标定草稿取消不替换原配置；启动被源故障/显式重连停止并收尾回到IDLE后，原START报告FAILED，下一次由用户显式发起的新START可继续。射频仍串行处理事务，进行中的START不能叠加第二条命令；不要把请求ACK当成已采集或已落盘。

启动/重连后尚未完成的速率检查显示为未就绪，START仍被阻止；只有已测得速率失败、ADC失败或已记录源错误才置质量故障，避免把正常等待永久锁成质量警告。
