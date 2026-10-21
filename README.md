# SwiftThrust

高速原始推力数据采集、TF日志与无线遥控采集/标定系统。GitHub仓库按用户指定拼写为SwiftTrust，本地项目名SwiftThrust。两板分别为STM32F411RCT6+ADS131M02+SDIO TF+E28，以及STM32G071KBT6+SSD1306+单编码器+E28。

当前已实现双板可执行参考应用：功能main、HAL时钟/外设/MSP/IRQ、ADC DMA与TF协作调度、无线会话/状态/启停/标定、OLED/编码器、MCU VDDA与四灯、片内Flash持久化配对及故障处理。打开 `SwiftThrust.code-workspace`，两个EIDE工程都选择 **ReferenceApp**。旧 **LinkCheck** 仅CRC/WFI，不能作为运行固件。

参考板配置与首次配对、接线、传感器配置、NV保留区和升级边界见 [REFERENCE_APPLICATION](Models/REFERENCE_APPLICATION.md)。稀疏HEX/ELF可由 `python -B Models/build_reference.py --artifacts` 生成到本地 `dist`；自动烧录仍拒绝执行。没有连接或烧录硬件，板子/电气/时序/TF最坏延迟与射频距离尚未上板确认。

默认无传感器规格时记录原始ADC，不伪造力值；标定需按真实传感器填写 [SENSOR.CFG.example](Models/SENSOR.CFG.example)。采集板PC0经1:2分压读取18650电压，PB6～PB9四灯分档，低于3.30V末灯闪烁；遥控SELF仍是MCU VDDA。PA0已输出自主5秒单次高电平，点火经过ARM及独立FIRE确认，外部执行级/点火反馈仍未知。两端自动发现、记住UID并重连，无需手动配钥。无线参数保持原值，仅开启高灵敏度LNA。完整字段、菜单及接线见[当前R7协议与流程](Models/LORA_PROTOCOL.md)。

离线验证覆盖点火去重/超时/中止、真实驱动寄存器模型、自动连接、四灯电量和原采集链；双板ReferenceApp ARM构建生成本地稀疏HEX/ELF。当前验证结果见`Models/firmware_host_validation.log`、`Models/link_runtime_validation.log`、`Models/ignition_pulse_validation.log`、`Models/reference_arm_validation.log`及[交接](HANDOFF.md)。没有硬件运行或距离测试结论。

**PCB尚未冻结，最终接线/极性、外部ADC时钟和传感器电气参数必须先核对。** 此版本可按参考配置安排上板验证，尚未实际板测。首次START及取消重启修复的失败→通过证据见 [专项审查](Models/FIRST_START_REVIEW.md)。

START待确认期间的STOP及提示小修已完成，详见 [STOP_ORDER_REVIEW](Models/STOP_ORDER_REVIEW.md)。本次交付后暂停开发，等待用户上板实测反馈。
