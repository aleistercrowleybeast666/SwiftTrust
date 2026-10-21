# 2026-10-22 R8交接

当前以[完整R8协议](Models/LORA_PROTOCOL_R8.md)和[R8交付](Models/R8_DELIVERY.md)为准。统一四行中英文菜单、共用任务状态和32位基础事务；2秒STATUS，Pair ID与固有UID绑定，掉线恢复原会话不打断日志。真实输出默认关闭，1秒仅候选模拟参数，硬件和真实时长待单独审核。原ADC DMA、FatFs、永久标定和NV边界保留；会话零点单独记录。没有烧录或实物验收。

以下内容全部为历史交接，不能覆盖R8现行协议和安全默认值。

# 2026-10-21 真实逻辑脉冲、自动连接与电池显示

当前实现以[完整R7协议与流程](Models/LORA_PROTOCOL.md)为准。采集板PA0/物理14脚/AF1 TIM2_CH1采用PWM2+OPM，自主高5秒后低；STOP/BACK可提前截止，重复FIRE不重启。ARM许可10秒，独立FIRE确认默认BACK；组合模式先等CAPTURING再ARM，正常脉冲结束继续记录，STOP/BACK收尾。无执行级反馈或物理保险输入，显示MCU状态不等于点火成功。

两端使用内置项目校验，首次空白设备0x0F/0x10自动发现并持久记住UID，再自动HELLO/COMMIT；旧NV损坏或另一UID不自动覆盖。固定密钥公开，仅用于减少误包风险。原射频参数和10秒心跳保持，仅开启高灵敏度LNA。

采集板PC0/物理8脚/ADC1_IN10通过10k+10k分压、100nF对地读取单节18650；VREFINT补偿VDDA，ADC1交替低速转换，ADS高速链路不变。PB6～PB9四灯分界3.90/3.70/3.50V，低于3.30V末灯1Hz闪烁，30mV回差及1/4 IIR；失效/过期全灭。采集STATUS mV改成电池电压，SELF保持遥控VDDA。

CubeMX已实际保存PA0单脉冲、PC0采样时间480周期；候选IOC时钟16MHz/PSC15，实际ReferenceApp 84MHz定时器/PSC83，均1MHz。实际初始化仍由reference_main和st_ignition_f4负责，未用Cube代码覆盖应用。当前未访问探针、串口或烧录。验证见本轮Models日志，下文为历史。

最终离线结果：主回归58组/55,498断言；无线owner10组/703断言；Flash/自动配对6组/76,654检查；实际F4脉冲驱动寄存器模型30检查；CubeMX/引脚与配置24测试通过。两板ReferenceApp ARM重建并生成稀疏HEX/ELF；采集FLASH65020B/RAM55624B，遥控FLASH56672B/RAM9992B，均不覆盖NV。没有上板或电气验证。

# 2026-10-21 协议阶段历史交接

已按用户要求保留原频点/功率/SF/BW/CR及普通链路时序，仅在两端无线初始化增加高灵敏度LNA。新增R6认证请求0x0D/回复0x0E、CMAC、独立命令/查询ID、缓存与有界重试，并接入双板ReferenceApp及点火页面。点火执行硬件固定UNBOUND，ARM/FIRE只拒绝，DISARM不冒充已确认关闭；没有点火GPIO或执行回调。

临时密钥通过本地串口IGNKEY设置到RAM，重启清除，不写NV/TF；配置后须显式CONNECT。完整帧格式及何时发送、接收、回复见[完整协议流程](Models/LORA_IGNITION_PROTOCOL_DRAFT.md)。本轮不访问硬件、不烧录；验证日志随本轮代码更新。下文保留先前采集应用交接。

# 2026-10-20 双板参考应用交接

当前工作已经从组件链接推进为两板ReferenceApp实际应用：两个 `board/reference_main.c` 是可执行入口，包含初始化、调度与IRQ，不再以CRC/WFI当完成。说明与接线以 [Models/REFERENCE_APPLICATION.md](Models/REFERENCE_APPLICATION.md) 为准。普通提交/推送后由交接回复给出实际SHA。

完整离线50组/54,398断言、无线7组/383断言和新增参考Flash模型均通过；有两板ARM构建日志及本地dist稀疏HEX/ELF。没有连接、测试或烧录硬件。后续需要真实板来验证电气、时钟、卡延迟、标定与RF性能；自动烧录入口保持拒绝。

选择ReferenceApp，首次通过本地UART INFO读取UID后双端PAIR，遥控STATUS→CONNECT，再采集页START/STOP。未配置真实传感器则raw-only。参考使用片内Flash，升级保留F411 sector2/3、G071 page62/63；不要烧录旧LinkCheck或跨NV区域BIN。点火只有静态菜单/未配置预留引脚。

本次仅修复 START 待确认时的 STOP 意图、顺序和提示，新增5组停止回归及1组无线运行时回归；详见 [STOP_ORDER_REVIEW](Models/STOP_ORDER_REVIEW.md)。代码交付后冻结，等待用户上板反馈；本轮未访问硬件。

以下是早期阶段的历史交接，描述的“缺少main/NV/集成”等已由本次实现替代；历史验证不等于当前硬件测试。

---

# SwiftThrust 当前交接

2026-10-20。源码、协议、四页面和原OLED选择已同步。新增真实HAL/diskio与链路持久化记录核心，详细接入依赖见[BOARD_INTEGRATION](Models/BOARD_INTEGRATION.md)。无线包无版本字段或版本位，TYPE=0x01…0x0C；`st2_`只是代码内部名称。当前目标为组件LINK_CHECK，**不是可运行整板固件**。所有硬件均未测试，未访问串口/SWD或机械臂设备。

## 逐模块状态

| 模块、真实入口 | 已实现且验证 | 已实现但待验证 | 尚未实现 |
|---|---|---|---|
| ADC解析/记录：`Acquisition/src/st_core.c`，`st_adc_decode`、`st_record` | Host：24bit符号扩展、CRC/状态/填充、记录CRC；两端ARM构建 | 真实ADS数据、SPI完整性及时间性能 | 实际电气/时钟/增益绑定 |
| ADS配置：`Acquisition/src/st_ads.c`，`st_ads_init`、`st_ads_tick` | Host总线模型：复位时序、延迟ACK、寄存器写入/读回、LOCK、启动排空、失败分支；F411 ARM构建 | `ports/st_ads_hal.c`真实DMA适配已F411编译链接；实际复位/读回/稳定采样未测 | 板级SPI/DMA/CLKIN初始化与配置阶段所有权接线 |
| DRDY采集：`Acquisition/ports/st_f4_capture.c`，`st_f4_capture_drdy/complete/error` | F411真实HAL编译链接；Host仅验证其下游队列和记录核心 | DMA/EXTI序列化、空闲排空、64bit时钟扩展、停止边界和实时性，未作HAL模型/板测 | 实际IRQ/DMA初始化与回调接线 |
| 环形与日志：`st_core.c`、`st_logger.c`，`st_ring_push/pop`、`st_logger_start/stop/service` | Host：SPSC溢出、双缓冲、保留在途内存、短写/异步故障、头/尾/同步/关闭顺序、恢复有效前缀；F411 ARM构建 | 真实卡延迟、分配簇、sync最坏停顿、掉电恢复 | 完整应用存储调度与持久化验收 |
| FatFs适配：`ports/st_fatfs_worker.c`，`st_fatfs_worker_step` | Host真实FatFs＋worker＋diskio在合成FAT16写入/校验1500样本；F411 ARM构建 | `ports/st_sdio_hal.c`真实DMA端口仅ARM验证，物理卡性能/掉电未测 | 板级SDIO/DMA/IRQ、卡检测极性和低优先级任务；旧NOT_READY-only占位已删除 |
| 无线短包：`src/st_link2.c`，`st2_server_receive/poll`、`st2_client_*`、`st2_details_*` | Host：长度/坏CRC/未知型、配对UID握手、持久化失败拒绝、幂等/终态ACK、状态心跳/年龄/不回绕、诊断一致快照/乱序/重读；两端ARM构建 | 与实际radio队列、TX_DONE、测量年龄及运行任务接线 | `src/st_nv.c`双槽记录核心已Host验证并两端ARM构建；实际Flash/EEPROM后端、配对/epoch域与写入时机未完成，无物理后端仍拒绝建链 |
| SX128x：`src/st_radio.c`，`st_radio_init/tick/send/receive` | 未修改Semtech源；Host总线模型和两端ARM构建，G071用有界PRIMASK标志交换 | 真实BUSY/SPI/DIO/复位/空中收发、超时恢复 | 真实`ports/st_radio_hal.c`已两端ARM构建；尚需SPI/GPIO初始化与应用收发调度 |
| 电压/LED：`st_core.c`电压函数、`ports/st_f4_capture.c`的`st_f4_voltage_leds` | Host：未知/过期/真实零、四级阈值与回差；F411 GPIO适配ARM构建 | 真实参考/分压/校准、LED极性和阈值 | 两板供电ADC采样绑定；未确认时UNKNOWN，GPIO未验证不驱动 |
| UI/OLED：`Controller/src/st_ui2.c`、`st_ui.c`、`st_raster.c`、`st_oled.c`，`st_ui2_handle/render` | Host：四入口浏览/返回、开始确认默认返回、仅采集启停、禁用页面不创建事务；SSD1306/SH1106总线模型；G071 ARM构建与真实C帧缓冲目视核查 | 实际屏幕/编码器电气和按键手感 | 板级输入/显示事件循环接线；当前字体ASCII，不冒充中文或实物照片 |
| UART：`src/st_debug.c`、`ports/st_debug_uart_hal.c`，`st_debug_write/service`、`st_debug_uart_configure` | Host验证有界发送队列/保留内存/满丢弃/限速；两板真实UART HAL编译链接 | 实际115200 8N1 TX/RX及错误恢复；UART HAL本身未做Host模拟/板测 | USART1 IRQ与HAL回调接线；没有RX命令执行器 |
| 导出：`Models/export_stlog.py`，`read_log`、`export` | Python：CRC、截断、未校准/已校准、坏ADC禁止力值、独占输出等边界 | 真实采集文件与UNDERLINE端到端导入 | 专用TR_F导出模式/原生STLOG解析器；现有CSV可用通用表格映射 |

| 标定：`st_cal*`、`ports/st_cal_system.c`、`ports/st_cal_fatfs.c`、`Controller/src/st_cal_remote.c` | Host端到端短包→只读样本→真实FatFs→生效/重挂载、逐扇区故障、四片幂等/丢回复核对、单编码器；双板ARM保留入口链接 | 实际ADC/TF/RF时序、供电和掉电恢复未板测 | 完整板级初始化/IRQ/owner循环及硬件参数仍待冻结；见`Models/CALIBRATION.md`和`Models/BOARD_BUILD_REVIEW.md` |

## 最新证据与复现

- 标定基线Host（e6589b0；本轮仅新增定向6组/313）：`python -B Models/run_host_tests.py`，**43组/40,371次C断言**，附Python导出边界；报告`Models/firmware_host_validation.log`含源码SHA256。`HOST_CC`指定Host GCC或将gcc加入PATH；`SDK_ROOT`还须提供真实FatFs R0.12c头/源码。
- ARM：`python -B Models/check_eide_link.py`，真实EIDE native builder读取两份实际工程源清单；**F41147个C文件＋1个启动汇编、G07142个C文件＋1个启动汇编，均成功链接且无未解析符号**。报告`Models/eide_link_validation.log`，时间2026-10-20T05:21 UTC。
- 本轮链接Flash/RAM：F411 **42,604/52,744 B**；G071 **38,616/7,288 B**。RAM含代表性静态上下文及链接脚本堆栈预留，未包含尚未接入的整板任务/运行栈，不是最终RAM或实时性证明。
- 引脚/资源：`python -B Models/validate_candidates.py`，**22项通过**，需本机CubeMX数据库。候选IOC曾由CubeMX实际保存核对，无代码生成或依赖整包下载。
- 两端`.eide/eide.yml`都有独立name/uid/output；打开`SwiftThrust.code-workspace`分别选目标构建。Custom烧录仅拒绝执行；不得将LINK_CHECK当成整板程序烧录。
- SDK为外部共享缓存，清单155文件/12,810,494 B，哈希本轮全数核对；SDK、工具链和产物不提交仓库。脚本临时构建目录自动清理，未在项目内留构建副本。

## 硬件与界面约定

采集器F411RCT6，控制器G071KBT6非N版。两板USART1都是PA9=TX、PA10=RX，与PA13/PA14 SWD独立；3.3V逻辑，TX/RX交叉、共地，VTREF不是外部供电入口。封装物理脚位及接线见`Models/DEBUG_UART.md`。

只读核实原机械臂文档/驱动/board_config，OLED恢复SSD1306、128×64、0x3C，旧硬件说明1.3寸；不锁定特殊卖家，不修改机械臂工程。主菜单状态/仅点火/仅采集/点火并采集，编码器进入/返回完整。两个点火页只有可浏览静态未实现说明和返回；没有启动空函数、触发调用链、点火包或输出接口。

## 记录与标定：事实及本轮方案

TF当前写`STLOG001`二进制：512B头、每样本32B、正常停止再32B尾。它不是XLSX/CSV/TR_F表格。原始ADC双通道、DRDY时间、序号、状态/质量/缺失数及CRC保留；不是只存计算后的力。

缓存为1024×32B环形队列=32KiB，加2×4096B写缓冲=8KiB，名义共1280条记录/40KiB。不是整个logger结构或整个应用RAM。下表是**纯记录容量计算上限**，默认缓冲从空开始；在途写不能重用、部分缓冲/同步/调度会缩短实际可承受停顿。不能直接把这个上限当TF卡延迟保证。

| 采样率 | 记录负载B/s | 环形队列时间 | 环形＋双缓冲名义时间 | 每4096B数据生成间隔 |
|---:|---:|---:|---:|---:|
| 2kSPS | 64,000 | 512ms | 640ms | 64ms |
| 8kSPS | 256,000 | 128ms | 160ms | 16ms |
| 16kSPS | 512,000 | 64ms | 80ms | 8ms |
| 32kSPS | 1,024,000 | 32ms | 40ms | 4ms |
| 64kSPS | 2,048,000 | 16ms | 20ms | 2ms |

2k是目标默认，8k是第一扩展候选；16/32/64k仅边界预算，不代表当前时钟、ADC、DMA或TF已支持。2k连续1小时约230.4MB（约219.7MiB），另加头/尾和文件系统开销。记录负载不含FAT/簇分配/元数据/同步成本。

写策略：4KiB满块优先，低速时约500ms允许刷部分块；每次service最多处理64条。文件创建与头写入/sync完成后才开采集；停止先关入口，再排空、写尾、sync、close。`f_sync`在已有完成写且距离上次同步≥1s时具备调度条件，但待写块优先，持续积压可能推迟同步，**不是硬性每秒落盘保证**。真实FatFs操作必须放独立低优先级存储任务，DRDY ISR不写卡。

丢帧可观察：环形满时drop-new计数、DRDY序号缺口、DMA忙/错误计数、ADC CRC/状态标志、时间倒退标志、日志CRC32及干净关闭尾记录。不会凭空恢复丢样；没有触发或未服务的硬件DRDY、首尾边界及一切漏采并不都能仅凭软件序号证明。sync完成也不证明卡内部在掉电时已永久保存。必须测实际卡的持续吞吐、最长write/sync停顿、队列高水位及最终序号/尾计数，不能以Host慢卡模型或标称卡速声称2k已板测通过。

标定核心、单编码器菜单、DMA只读样本投递、TF双文件参数后端与有界无线交互已实现并通过Host/组件链接，见[CALIBRATION](Models/CALIBRATION.md)。完整板级应用和物理验收未完成；以下物理验收要求继续适用。

1. 选择并绑定传感器profile，保存身份/修订、单位N、线数、量程、ADC增益/时钟/OSR/滤波、激励方案。条件变化标记失效，不能悄悄复用旧系数。
2. 空载稳定采样检查噪声/饱和/CRC后求零点r0；施加可追溯已知力Fref求r1，`k=Fref/(r1-r0)`，`F=k*(raw-r0)`；拒绝分母太小/不稳定/超范围结果，可选额外载荷点复核。静态校准不证明动态带宽。
3. 校准完成后明确审阅确认，保留旧profile；采集期间锁定快照，停止后才允许更改。保留原始raw以便重算，不能覆盖原始通道。
4. TF参数文件建议双槽/带序号和CRC的记录：写新内容、sync、关闭、读回验证后再切换有效记录；掉电恢复选最新完整有效项，失败保留旧项，缺失/损坏显示未校准。FatFs rename/卡缓存不是天然掉电原子保证，需实测。该参数库与无线NV计数后端分别管理，不能混用。
5. 现有日志头已含profile ID/修订、zero、nN/count斜率、两路增益、线数、时钟/OSR及calibration_valid；没有完整传感器序号/日期/激励/滤波资料。未来可把完整profile作为TF伴随文件、在采集时固定关联；记录格式若需扩展，先单独说明兼容影响。

UNDERLINE只读核实：TR_F是无表头两列`时间,原始值`，默认秒，单位raw，无内置标定系数；现有STLOG导出CSV为多列，可用Generic Delimited映射时间us、raw或已验证force_N。直接TR_F兼容需另行导出两列raw＋配套标定profile，不能只改扩展名；本轮未添加该转换功能或修改UNDERLINE。

## 完成整板固件仍需

真实原理图/时钟/分压/LED/传感器参数确认；板级初始化、中断、独立存储任务和完整应用循环；SDIO/ADC/RF适配器的板级初始化和IRQ/任务接入；双槽记录的实际NV介质、配对与epoch恢复/写入窗口；电气、实时性、掉电与射频实测。当前源码可审阅和组件构建，不宣称整板完成。第三方保持原许可，未擅自添加项目新许可。

块设备512B对齐bounce属于已有日志数据的副本，日志40KiB/1280条独有记录预算不变；标定另有2048B样本队列与1024B参数工作帧，已计入代表性ARM RAM对象。新增非点火标定TYPE；原有包及STLOG001布局不变，无点火执行。

整板编译可行性与确切缺口见[BOARD_BUILD_REVIEW](Models/BOARD_BUILD_REVIEW.md)。校准TF记录已实现；链路epoch/counter的物理NV后端仍未选定，两者不可混用。


无线应用接线已收口到`st_link_runtime`；新增定向Host6组/313断言及当前双板链接通过，原43组报告对应e6589b0基线，未重跑。已可用范围、调用顺序及仅四组外部输入见[BOARD_DECISIONS](Models/BOARD_DECISIONS.md)，本轮完成后停止扩展。
