> 历史评审记录：本文中的旧ADC、时钟及引脚假设已由[2026-10-29双板硬件契约](REFERENCE_APPLICATION.md)替代；当前测试/交付见[R8硬件适配报告](R8_HARDWARE_ADAPTATION_20261029.md)。历史结果不代表当前固件或实板验证。

> 历史阶段记录。当前OLED/菜单/协议/实现状态以../HANDOFF.md、OLED_REFERENCE.md和../PROTOCOLS.md为准。

# SwiftThrust v0.1 复核与候选收敛

本轮只收敛候选和需求，不进行板上验证、烧录、点火实现或大依赖下载。2026-10-19核对。候选不等于最终电气规格。

## ADS131M02：具体输入条件与推荐组合

依据[TI ADS131M02 SBAS853A RevA](https://www.ti.com/lit/ds/symlink/ads131m02.pdf)，印刷第5页§6.3推荐工作条件、第18页式(3)：高增益G∈{8,16,32,64,128}时，两根输入各自必须满足

`AGND−1.3 V ≤ AINP, AINN ≤ AVDD−1.8 V`

同时 `|Vdiff| ≤ 1.2 V/G`。这不是用绝对最大额定值作工作界限。以AGND=0、Vcm=(AINP+AINN)/2、Vdiff=AINP−AINN写成共模约束：

`−1.3+|Vdiff|/2 ≤ Vcm ≤ AVDD−1.8−|Vdiff|/2`。

低增益G=1/2/4时，单端上限改为AVDD，差分满量程仍是±1.2/G。芯片正常AVDD必须2.7–3.6V，2.5V桥激励不能当ADC模拟供电。

| 条件（对地对称桥） | 平衡Vcm | 高增益单端上限 | 判断 |
|---|---:|---:|---|
| AVDD=3.0V，EXC=2.5V | 1.25V | 1.20V | 不成立，零差分也超过50mV |
| AVDD=3.3V，EXC=2.5V | 1.25V | 1.50V | 标称成立；零差分上端余量250mV，再扣信号半幅/误差 |
| AVDD=3.0V，EXC=2.0V | 1.00V | 1.20V | 数学上可作备选；须传感器允许2.0V激励，输出幅度相应降低 |

因此明确纠正：**2.5V桥激励本身不与ADS131M02冲突；3.3V AVDD+2.5V桥是当前推荐候选。**不匹配的是3.0V AVDD+高增益+约1.25V输入共模这一组合。桥共模并不总精确等于EXC/2；桥阻差、接线电阻、地电位和负载会改变它，仍需真实传感器规格和电路校核。

候选供电分工为ADC AVDD=3.3V、DVDD=3.3V、桥EXC=2.5V。单18650的稳压、模拟净化、桥驱动能力、负载共享/反灌与充电芯片未选。举例做误差预算，若电路未来能保证AVDD最低3.20V、EXC最高2.525V，则对称桥Vcm=1.2625V：G=8差分满量程半幅75mV时上端仍余62.5mV；G=128半幅4.6875mV时余132.8125mV。**3.20/2.525只是预算示例，不是已选器件公差或板上测量**；必须另计桥共模偏差、保护/缓冲误差和瞬态。

CH2推荐候选为PGA1、有效差分缩放k=0.4：2.5V激励映射1.0V，2.525V映射1.01V，低于±1.2V差分范围。电阻、RC、缓冲/保护均未选；低增益输入阻抗会加载分压，须按负载后的有效比例标定，不能只按空载电阻比计算。六线远端SENSE±需高阻差分网络或缓冲；四线只做本地激励监测，不能声称补偿了电缆压降。

## 激励为什么影响灵敏度与读数

mV/V是“每1V激励，在额定载荷时输出多少mV”。例如额定2mV/V的桥，2.5V激励对应额定输出5mV，3.3V激励对应6.6mV；这只是在传感器允许该激励且其它条件满足时的名义值。提高激励同时提高共模，桥电阻不变时发热功率按EXC²/R增大。灵敏度、共模、功耗都要核对，不能只看输出变大。

ADS使用内部1.2V参考，激励变化不会自动被硬件比率参考抵消。软件需有有效激励测量和标定。名义换算为`F/F额定≈(Vdiff/Vexc)/(S×10^-3)`，S单位mV/V；可信N值还要整机已知载荷校准，而非凭铭牌直接声明测量精度。

普通四/六线桥没有统一数字身份接口；六线SENSE不是存储灵敏度的总线。软件不猜型号、量程或灵敏度。更换传感器档案与校准向导需求见[SENSOR_CALIBRATION_REQUIREMENTS.md](SENSOR_CALIBRATION_REQUIREMENTS.md)，本轮不扩展实现。

## G071精确料号已处理

保持STM32G071KBT6、GP不带N、LQFP32 7×7mm。STM32G071KBT6N也为LQFP32，区别不是脚数或外形，而是部分焊盘信号：

| 物理pin | KBT6（当前） | KBT6N |
|---|---|---|
| 17 | PB2 | PB15 |
| 26 | PA15 | PD0 |
| 27 | PB3 | PD1 |
| 28 | PB4 | PD2 |
| 29 | PB5 | PD3 |

依据[ST DS12232 Rev5第38页图5及第139页订货信息](https://www.st.com/resource/en/datasheet/stm32g071kb.pdf)与本机官方器件XML。当前无线SPI1使用PB3/4/5和PA15 CS，故BOM、原理图符号和采购不能换N。现有ioc已保存精确Mcu.CPN=STM32G071KBT6，未改成其它型号；GP/N混用负例会被Host核查拒绝。

## F411的100MHz与SDIO域

[RM0383 §6.3.2及SDIO章节](https://www.st.com/resource/zh/reference_manual/rm0383-stm32f411xce-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)说明SDIO域来自主PLL的Q输出，需≤48MHz；USB若启用则要求准确48MHz。PLL同一VCO经P给系统、Q给SDIO。整数P/Q无法同时获得恰好100MHz系统和恰好48MHz Q输出。以下仅为可行时钟算术候选，尚未写入ioc：

| 候选 | VCO/P/Q示例 | 系统 | SDIO域 | 常速SD_CK示例 |
|---|---|---:|---:|---:|
| A | VCO192MHz，P2，Q4 | 96MHz | 48MHz | CLKDIV0→24MHz |
| B | VCO200MHz，P2，Q5 | 100MHz | 40MHz | CLKDIV0→20MHz |

A便于以后保留准确48MHz域；B可支持当前SDIO但不能把40MHz当USB合格时钟。VCO输入源、晶体/HSI精度、电压档、Flash等待周期与APB分频还要完整验证；上述不构成已完成的CubeMX时钟树。初始化卡时应≤400kHz；常速SD总线≤25MHz，并满足PCLK2与SD_CK约束。

ADS正常转换必须持续CLKIN，但不一定要独立振荡器。优先考虑ADC旁的3.3V LVCMOS 8.192MHz有源时钟候选；不能只接无源晶体。HR、8.192MHz、OSR2048对应标称2kSPS；实际时钟误差也必须记入采样率。MCU输出8MHz配同OSR是1953.125SPS，不应标成2kSPS。

PA8/MCO1只是已预留选项，尚未证明能产生8.192MHz。MCU方案可进一步算PLLI2S→MCO2：例如HSE8MHz、M5/N128/R5，再MCO2除5，算术上为8.192MHz；MCO2在PC9，与当前SDIO_D1冲突，需要把D1候选改到PA8并重新验证布线/时序。这条路线没有应用到ioc或做抖动/板验，不能称已配置。当前仍保留PC9作TF D1。

当前两端ioc的SYSCLK/HCLK均为默认16MHz；F411 APB1/APB2也为16MHz，SPI1/SPI2默认计算8Mbit/s。F411中PLLCLK=96MHz、PLLQCLK=48MHz是保存的默认计算字段，不代表已配100MHz时钟或已验证HAL时钟初始化；HSE_VALUE=25MHz未选择晶体，PH0/PH1仍复位态。G071的HSE_VALUE=8MHz、PLL输出64MHz同样为默认值，系统仍16MHz。PA8未输出ADC时钟，采集时间基、DMA、NVIC优先级未配置。

F411实际已存管脚：ADC SPI1 PA5/6/7，PA4 CS、PC4 DRDY、PC5 SYNC；TF PC8/9/10/11/12和PD2四位；无线SPI2 PB13/14/15、PB12 CS、PC6 BUSY、PC7 IRQ、PB2 RESET；电池PC0，SWD PA13/14。DMA仅候选：SPI1 RX D2S0C3、TX D2S5C3；SDIO RX D2S3C4、TX D2S6C4；无线SPI2 RX D1S3C0、TX D1S4C0；MCU ADC若用DMA可D2S4C0。逐项数据库核对通过，但ioc里没有Dma请求配置。

## 采样上限、缓冲与FreeRTOS

没有用户已确认的最高采样率，不能补成已定案。建议首版先验收默认2kSPS，可把8kSPS作为首轮扩展验证上限候选；32/64kSPS只是资源边界预算。TI首页和§6.5支持最高64kSPS，CLOCK寄存器第50页TBM=1选择OSR64；[TI官方技术答复](https://e2e.ti.com/support/data-converters-group/data-converters/f/data-converters-forum/1589450/ads131m02-ads131m02-inquiry)确认HR 64kSPS需TBM=1。RevA第20页表8-2的HR/OSR64行却写32kSPS，与式(5)/特性描述不一致；不单凭该表静默承诺整机64k。必须按寄存器回读与DRDY/完整SPI帧实测验收。

以下按每组双通道12字节ADC帧、32字节候选日志记录计算，不含文件系统额外开销；32KiB环形即1024条记录：

| kSPS | ADC kB/s | SPI纯载荷 Mbit/s | 日志 kB/s | 32KiB环形可容纳 | 250ms停写至少需环形KiB |
|---:|---:|---:|---:|---:|---:|
| 2 | 24 | 0.192 | 64 | 512ms | 15.625 |
| 4 | 48 | 0.384 | 128 | 256ms | 31.25 |
| 8 | 96 | 0.768 | 256 | 128ms | 62.5 |
| 16 | 192 | 1.536 | 512 | 64ms | 125 |
| 32 | 384 | 3.072 | 1024 | 32ms | 250 |
| 64 | 768 | 6.144 | 2048 | 16ms | 500 |

250ms仅为说明资源边界的停写场景，未测TF最坏延迟。16k的125KiB已几乎耗尽F411全部128KiB SRAM；32/64k无法以当前32字节记录、片内RAM缓存该停写时长。可以优化格式/预分配/卡选择和写入策略，但不能据平均速度承诺不丢样，溢出必须计数/告警或明确停止。

采集端建议静态FreeRTOS作为后续HAL/FatFs原型的优先评估方案：高优先级采集处理、单一TF写入任务、低优先级无线状态任务，静态栈/TCB/队列、无采样路径运行期分配。2k时32KiB环形+2×4KiB写缓存+8–16KiB任务/中断栈预算，另留FatFs和全局数据；8k若要覆盖示例250ms停写，环形宜至少64KiB，64+8+16+8≈96KiB，约余32KiB给其它全局/TCB等，仍待链接map和栈水位。无线丢旧状态不堵ADC；DRDY不是RTOS tick。若TF全链路可拆成真正非阻塞状态机，裸机也可，RTOS本身不降低卡忙或长期关中断风险。遥控端仍优先事件循环与分片OLED刷新。

## 缺失SDK、缓存与磁盘估计

当前ioc引用STM32Cube FW_F4 V1.28.3和FW_G0 V1.6.3。检查范围为<USER>/STM32Cube/Repository及<USER>/.stm32cubemx；未发现完整F4/G0包或对应zip。Repository只有完整C0 V1.4.1、C0的v140/v141压缩包与空TMPUPD目录。CubeMX器件/时钟数据库可继续复用做pinout；updater XML/PDSC只是元数据，C0包不能作为F4/G0的HAL/CMSIS，AZRTOS-F4/G0描述文件也不等于MCU固件包。

本机已缓存的官方`plugins/updater/STMUpdaterDefinitions.xml`实际标注：

| 请求目标 | 基包 | 补丁 | 从零按此路径下载量 |
|---|---|---|---:|
| F4 V1.28.3 | FW.F4.1.28.0：611MB | FW.F4.1.28.3：314MB | 925MB |
| G0 V1.6.3 | FW.G0.1.6.0：203MB | FW.G0.1.6.3：168MB | 371MB |

合计约1296MB（保留元数据的MB标注），属于约1.3GB级下载；实际安装可能依据分发方式变化。完整包未下载，无法报精确解压量。结合现有C0 zip解压约2.5倍的参考比例，建议为下载、解压和补丁临时目录预留约5–7GB峰值空间，这是估算，不是F4/G0实测。当前观察C盘约29.9GiB空闲，D盘约22.8GiB；中央Repository在C盘。未来只安装各一份，工程引用/复制必要文件，不复制全套Examples或建工作副本。本轮不下载这些包，也不锁定软件包最终版本。

## 本轮实际落地

G071候选ioc保留精确KBT6 GP。采集候选ioc由真实CubeMX重新加载/保存，增加带CANDIDATE后缀的AVDD3300mV、DVDD3300mV、EXC2500mV和CH2缩放400000ppm注释；SWIFTTHRUST_BOARD_BOUND仍为0。它们不是外部ADC寄存器写入，也不选择电阻或启用真实输出。其余时钟/DMA/电源器件/PGA保持未定。Host核查21项通过；没有ARM编译或板上实测。
