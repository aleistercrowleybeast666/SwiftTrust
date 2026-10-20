# SwiftTrust 当前可用范围与最小外部依赖

2026-10-20。本轮只收口既有非点火逻辑与射频应用接线，完成后停止扩展。没有访问板卡、探针、串口、ADC、TF或射频设备，也没有代选尚未冻结的硬件参数。

## 已可复用的范围

- 原始ADC解码/校验、DRDY DMA捕获适配、40KiB独有日志缓存、真实FatFs/diskio/SDIO适配、STLOG001及通用CSV导出。
- 卸载归零/已知质量或力标定、单编码器输入、只读样本队列、TF双文件校准参数、绑定核验、采集中拒绝替换及丢回复后结果核对。
- 两板实际HAL/Semtech组件、会话/幂等短包/四片快照、OLED/UI及独立调试UART适配。
- 本轮`st_link_runtime_init`、`st_link_server_step`、`st_link_client_step`已把既有协议与射频收发连接起来：忙时保留一份已生成回复，最多一份driver排队；实际TX_DONE/timeout计数驱动重试。控制器失效/换会话的未发射排队请求在下一次driver tick前撤销；已经在空中的请求不能撤回。射频故障后不自动建链或重发START。

这些是可集成的软件组件和离线验证结果。`board/link_check_main.c`仍只CRC/WFI，自定义烧录入口仍拒绝执行，**不是可运行整板固件**。

## 只需补齐四组外部输入

| 必须提供/确认的内容 | 涉及文件/接口 | 为什么阻塞整板接入 | 现有依据与建议状态 |
|---|---|---|---|
| 实际板版原理图或确认冻结候选引脚，并给出供电/复位、SD卡检测极性、MCU时钟接法 | `Models/config_pending.json`的`board_bound`及两板`board/link_check_main.c`；`st_*_hal_bind`、SDIO/I2C/EXTI/DMA初始化 | 目前IO/IOC只经过器件数据库校验，不能推断真实电平、上拉、复位或卡检测；错误初始化可能直接改变硬件电气状态 | F411RCT6/G071KBT6、ADS131M02、E28-2G4M12SX、SSD1306 128×64/0x3C已确认；引脚表仍标candidate。可确认整份候选图，不必逐个寄存器选择 |
| ADC CLKIN实际来源/频率，以及传感器型号/量程/mV/V/激励额定、实际4/6线接法和激励电路 | `Models/config_pending.json: acquisition`；`st_ads_config`、`st_cal_binding`、`st_log_config` | 未知时钟不能报告真实采样率；未知桥路和输入范围不能确定PGA及有效校准绑定。还需实测稳定窗口，不能用Host阈值充当精度结论 | 2kSPS为已知目标。8.192MHz/OSR2048/PGA128及2.5V分别是合成测试/硬件候选，不是已批准实物参数。得到实际资料后可计算OSR/PGA，用户无需先手选全部寄存器 |
| 最终RF频点/功率和SF/BW/CR，是否沿用旧参考或确认新候选 | `Models/config_pending.json: radio.active_profile`；`st_radio_config`、`st2_timing` | 双端必须一致，实际空中时间决定TX timeout/ACK窗口；不能仅因旧距离记录就冻结新配置 | 旧参考2473MHz、+12dBm、SF10/BW812.5k/4/5；SF11/BW406.25k仅为候选。超时可根据选定配置及现有Semtech空中时间结果推导；没有当前距离保证 |
| 链路NV介质，以及现有引导/升级是否需保留该区、采集中能否建立新会话 | `Acquisition/src/st_nv.c`、`st2_nv_port`、两板`board/memory.ld` | counter/epoch要求真实持久提交；控制器无TF，采集板Flash写入还涉及采样取指停顿。当前没有物理后端，建链会明确拒绝 | 已有双槽96B格式与故障测试；介质/Flash保留区未冻结，不建议任意地址。介质及升级约束确定后，可由软件侧推导布局，不需要用户手写地址 |

电压/四灯可先保持UNKNOWN/关闭，**不阻塞原始码采集链的实现**；若需要实测电压及四灯分级，再提供电池/供电类型、实际分压和LED极性/阈值依据。不会将缺失测量填成0或满电。传感器普通4/6线桥路也不自动产生型号/量程身份。

板级初始化、IRQ路由与任务调度仍是后续软件工作，受上述硬件输入约束；不把RTOS品牌、任意引脚地址或校准统计阈值都变成用户必须先回答的问题。参数确认并不等于整板工作已完成。

## 应用接线与调度约束

采集侧先完成实际GPIO/SPI/SDIO/时钟/DMA并验证，配置ADS并确认寄存器读回、SPI配置owner空闲，才把IRQ交给capture。启用DRDY前绑定`st_f4_capture_bind_calibration`；`st_cal_system_init`恢复参数并挂接server。由同一串行控制owner调用`st_logger_service`、`st_cal_endpoint_step`、`st_link_server_step`，避免并发修改logger状态。阻塞FatFs log worker另设低优先级执行上下文；标定只在logger IDLE、worker FREE后临时取得卷的排他访问，不与worker并发调用FatFs。不能把单while顺序写卡当成已解决延迟。

控制器先处理本次输入/显式connect，`st_cal_remote_step`只在菜单可见时准备有界查询，再调用`st_link_client_step`。两个runtime step各自包含一次`st_radio_tick`；同一无线owner不要再额外并发tick/send/receive。DIO ISR仅调用`st_radio_irq_isr`。SPI/BUSY wait的yield不得递归调用该runtime。时钟和随机抖动由板级输入；runtime不生成频率、供电、NV或START默认值。

## 验证和本轮结束边界

上一标定基线提交`e6589b042d47c73a194eac2f48576cbe5d6be40a`的Host43组/40,371断言不重复运行；报告`firmware_host_validation.log`对应那个基线。当前新增接线只运行`check_link_runtime.py`：**6组/313断言**，源码哈希见`link_runtime_validation.log`；使用真实未修改Semtech代码和软件射频事件模型，不能当成板测。新代码也在双板真实EIDE清单中保留入口并链接，见`eide_link_validation.log`和`BOARD_BUILD_REVIEW.md`。

本地执行服务中途断连，只读命令失败；连接恢复后已核实结果再继续，没有重复写入。当前没有权限拒绝。待四组外部输入及可核实的实际板测环境到位后才能完成整板启动/时序/掉电/射频验证。本轮不继续拓展功能，所有点火执行/触发/输出链保持未实现。
