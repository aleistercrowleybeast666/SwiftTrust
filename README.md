# SwiftThrust

高速原始推力数据采集、TF日志与无线遥控采集/标定系统。GitHub仓库按用户指定拼写为SwiftTrust，本地项目名SwiftThrust。两板分别为STM32F411RCT6+ADS131M02+SDIO TF+E28，以及STM32G071KBT6+SSD1306+单编码器+E28。

当前已实现双板可执行参考应用：功能main、HAL时钟/外设/MSP/IRQ、ADC DMA与TF协作调度、无线会话/状态/启停/标定、OLED/编码器、MCU VDDA与四灯、片内Flash持久化配对及故障处理。打开 `SwiftThrust.code-workspace`，两个EIDE工程都选择 **ReferenceApp**。旧 **LinkCheck** 仅CRC/WFI，不能作为运行固件。

参考板配置与首次配对、接线、传感器配置、NV保留区和升级边界见 [REFERENCE_APPLICATION](Models/REFERENCE_APPLICATION.md)。稀疏HEX/ELF可由 `python -B Models/build_reference.py --artifacts` 生成到本地 `dist`；自动烧录仍拒绝执行。没有连接或烧录硬件，板子/电气/时序/TF最坏延迟与射频距离尚未上板确认。

默认无传感器规格时记录原始ADC，不伪造力值；标定需按真实传感器填写 [SENSOR.CFG.example](Models/SENSOR.CFG.example)。电压显示是MCU VDDA，不是电池SOC。点火仅保留静态菜单和未配置PA11/PA12标记，没有执行、协议或GPIO输出。

完整离线回归50组/54,398断言及导出检查、无线owner7组/383断言、新增Flash/配置故障模型均通过；双板真实ReferenceApp ARM编译通过。具体证据见 [构建审查](Models/BOARD_BUILD_REVIEW.md) 和 [交接](HANDOFF.md)。无 `.work`、SDK副本或额外远端仓库。

**PCB尚未冻结，最终接线/极性、外部ADC时钟和传感器电气参数必须先核对。** 此版本可按参考配置安排上板验证，尚未实际板测。首次START及取消重启修复的失败→通过证据见 [专项审查](Models/FIRST_START_REVIEW.md)。

START待确认期间的STOP及提示小修已完成，详见 [STOP_ORDER_REVIEW](Models/STOP_ORDER_REVIEW.md)。本次交付后暂停开发，等待用户上板实测反馈。
