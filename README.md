# SwiftThrust R8

高速原始推力采集、TF日志与无线遥控系统。仓库名为SwiftTrust，本地项目名SwiftThrust。采集端STM32F411RCT6 + ADS1220 + SDIO/FatFs；遥控端STM32G071KBT6 + SSD1306 + 编码器；无线E28-2G4M12SX。

R8统一四行菜单、基础任务状态、Pair/UID/会话/32位事务和七类无线消息。默认中文（所需中文字模子集），可编译英文。进入子菜单和START确认均默认“返回”；正常返回、状态过期及重连不停止采集，STOP等TF sync/close完成才确认。采集与点火中的执行动作需独立确认，有限动作结束继续采集。仅点火页没有停止菜单，不显示“点火成功”。

**PA0保留已确认的1000ms阻性负载配置与独立ARM/FIRE确认、故障截止。** 本次仅验证代码和离线模型。未烧录、未接入硬件测试，无无线距离或点火安全验收结论。

- [2026-10-29硬件适配交付](Models/R8_HARDWARE_ADAPTATION_20261029.md)
- [完整R8协议、所有收发触发条件与菜单流程](Models/LORA_PROTOCOL_R8.md)
- [R8交付、测试与修改清单](Models/R8_DELIVERY.md)
- [日志STLOG001规范](PROTOCOLS.md)
- [参考应用、NV分区与构建接入](Models/REFERENCE_APPLICATION.md)（硬件基础资料；R8行为以最新协议为准）
- [单传感器配置示例](Models/SENSOR.CFG.example)

编译期参数集中在 `Common/Inc/st_project_config.h`；PCB映射在各自board/BSP。每套设备的32位Pair ID需不同，两端需一致；首次保存实际STM32 UID后拒绝另一设备接管。射频保持2473MHz/+12dBm/SF10/BW812.5kHz/CR4/5，CRC、高灵敏度LNA和AGC保留；STATUS每2秒。采集端PB10、遥控端PB0链路灯适配最新CAD，正常常亮、4秒慢闪、7秒快闪，驱动故障双短闪；实物引脚/极性仍待核对。

ADC DMA、SDIO/FatFs协作调度、STLOG001原始记录、SENSOR.CFG/CAL0.BIN/CAL1.BIN永久标定保留。可选本次会话零点与永久斜率分开，不在采集中持续追零；原始码不变。自动置零默认关闭；菜单开关仅本次上电有效，启动默认值可在SENSOR.CFG配置。

R7无线与R8不兼容，需两端一起升级；不清除原NV配对及计数区域，不改变升级边界。STLOG001在保留区追加ADC1单通道元数据，SZR1会话置零保留；新版导出器同时读取历史双通道日志。旧[R7协议](Models/LORA_PROTOCOL.md)仅供历史参考。

打开 `SwiftThrust.code-workspace`，两端EIDE选择 **ReferenceApp**。旧LinkCheck不是运行固件。离线检查：

`python -X utf8 -B Models/run_host_tests.py`，英文加 `--english`；另运行 `check_link_runtime.py`、`check_reference.py`、`check_ignition_f4.py`、`validate_candidates.py`。配置SDK_ROOT、HOST_CC、ARM_GCC_ROOT后，`python -X utf8 -B Models/build_reference.py --artifacts`生成本地稀疏HEX/ELF到dist，不自动烧录。

PC0与遥控PA3均使用10k/10k电池分压及VREFINT补偿；GUI显示实际本地电池，VDDA独立用于供电有效性。F411改为8.192MHz HSE、96MHz系统时钟和PA8 MCO1 4.096MHz；ADS1220理论连续速率1976.834SPS。硬件依据2026-10-29双板epro2，仍需实板测波形、持续记录和误差。
