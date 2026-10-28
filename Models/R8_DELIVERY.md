> 历史评审记录：本文中的旧ADC、时钟及引脚假设已由[2026-10-29双板硬件契约](REFERENCE_APPLICATION.md)替代；当前测试/交付见[R8硬件适配报告](R8_HARDWARE_ADAPTATION_20261029.md)。历史结果不代表当前固件或实板验证。

# SwiftThrust R8 交付

2026-10-22。基于 GitHub main 起点463011bbd5a6210257f275202c7f1b4cf823d3f7，修改完成后提交并推送。此文档与变更在同一提交内；最终SHA见交付消息及 git log -1。

## 实现范围

- 四行固定16像素的中英文菜单，共用任务枚举和模式策略；确认默认返回、滚动、长按、空项跳过，运行状态位于开始行。
- R8唯一基础帧、七类逻辑消息、32位Pair/Session/Transaction/STATUS序号；绑定实际UID，恢复原会话，重复结果缓存和迟到回复隔离。
- START匹配有效记录结果，STOP在TF sync/close后完成；STOP可取消待完成START和未发出的ARM/FIRE。普通断线及射频故障不停止本地记录，ADC/TF真实故障保护保留。
- STATUS每2000ms；PB0链路LED，4秒慢闪、7秒快闪、驱动故障双短闪。保持原射频物理参数、LoRa CRC、高灵敏度LNA和AGC。
- 单传感器永久TF标定保留；本次稳定零点单独记录，原始码和永久斜率不变。SZR1兼容扩展及新旧日志导出已验证。
- 真实输出默认关闭，取消分散写死的5秒时长；1秒仅为软件模拟负载候选。保留故障截止接口，真实硬件启用和执行时长须单独审核。

完整协议、所有字段、收发触发与菜单流程见 [LORA_PROTOCOL_R8.md](LORA_PROTOCOL_R8.md)。编译配置见 [st_project_config.h](../Common/Inc/st_project_config.h)。

## 验证结果

| 检查 | 结果 | 证据 |
|---|---|---|
| 中文 Host / 协议 / UI / ADC / 日志 / 真实FatFs软件模型 / 标定 | 60组，57,547条C断言通过 | [日志](firmware_host_validation.log) |
| 英文同套 Host 回归 | 60组，57,547条C断言通过 | [日志](firmware_host_validation_en.log) |
| 无线完整驱动状态机运行模型 | 12组，931条断言通过 | [日志](link_runtime_validation.log) |
| NV/身份/断电切点/自动配对 | 6组，76,671条检查通过 | [日志](reference_host_validation.log) |
| 实际F4输出驱动寄存器模型 | 26条检查；默认禁用、拒绝启动及低电平路径通过 | [日志](ignition_pulse_validation.log) |
| CubeMX候选资源、封装与引脚 | 25项通过；两端PB0保存为GPIO_Output且默认低 | [资源回归](r8_pin_validation.log)、[采集CubeMX](cubemx_r8_acquisition.log)、[遥控CubeMX](cubemx_r8_controller.log) |
| 双板 ReferenceApp ARM/EIDE | 完整编译链接、强IRQ入口、无未解析符号、NV区无加载覆盖通过 | [日志](reference_arm_validation.log) |
| Python导出器 | 新旧头、SZR1零点隔离、有效/无效标定、坏CRC、断尾、排他输出通过 | 同Host日志 |

F411应用Flash 68,972B/192KiB、RAM 57,640B/128KiB；G071 Flash 61,536B/124KiB、RAM 10,264B/32KiB。链接内存统计包含现有栈/堆预留，不能代替板上栈峰值测量。官方HAL原有unused-parameter警告保留；项目Host代码使用-Werror、conversion、shadow检查。

中英文预览来自实际C渲染器的1024B SSD1306帧缓冲，已检查字模、滚动和反色；所需中文字模123个，位图共2,952B，12×14像素，完整字体未引入固件。

- [中文菜单](ui_host_preview.png) / [英文菜单](ui_host_preview_en.png)
- [中文标定](calibration_ui_preview.png) / [英文标定](calibration_ui_preview_en.png)
- [中文有限任务状态](ignition_ui_preview.png) / [英文有限任务状态](ignition_ui_preview_en.png)

## 保留、迁移与限制

ADC DMA、SDIO/FatFs所有权、STLOG001的512B头和32B原始记录、CAL0.BIN/CAL1.BIN格式、Flash NV分区与固件升级边界保留。原始ADC/SDIO端口、Flash日志后端和Semtech/tiny-AES上游代码未修改。R1旧参考协议仅用于历史Host兼容回归，运行固件接收路径只接受R8。

R7无线与R8不兼容，须两端同时升级，不自动降级；保留已绑定UID和计数，禁止升级时全片擦除NV。日志仅在原保留区追加SZR1，旧导出器仍能按头36的有效零点读取；新导出器额外报告永久/本次零点。R7文档已标为历史。

自动置零默认关闭；菜单开关只对当前上电有效，重启默认由SENSOR.CFG的可选AUTO_ZERO决定。它不改写永久标定文件。稳定窗口不能证明无载荷，操作员仍须确认；实际更换传感器须更新SENSOR_ID并重新标定。

采集端重启后的旧任务结果不能凭空恢复，保守显示UNKNOWN；不会自动START或重放输出。采集任务可由明确STOP完成当前状态确认后再开始。缓存窗口外的旧事务返回OLD而非重新执行；危险动作未知时只查询或安全撤销。

仍未实物验证：PCB引脚与极性、PB0/PC0接线、18650电压与LED门限、ADC时钟与噪声/零点跨度、DMA/SDIO时序、TF卡最坏延迟和断电行为、OLED实际清晰度与输入手感、无线丢包/距离/双向超时，以及物理断能、执行级、外部反馈和真实点火安全。本轮未访问串口/SWD、未烧录、未连接火工品；软件模型通过不构成上述验收。

## 完整修改文件清单

- [Acquisition/.eide/eide.yml](../Acquisition/.eide/eide.yml)
- [Acquisition/SwiftThrust_Acquisition_Candidate/SwiftThrust_Acquisition_Candidate.ioc](../Acquisition/SwiftThrust_Acquisition_Candidate/SwiftThrust_Acquisition_Candidate.ioc)
- [Acquisition/board/reference_main.c](../Acquisition/board/reference_main.c)
- [Acquisition/configure_candidate.mx](../Acquisition/configure_candidate.mx)
- [Acquisition/ignition_candidate.mx](../Acquisition/ignition_candidate.mx)
- [Acquisition/include/st_auto_link.h](../Acquisition/include/st_auto_link.h)
- [Acquisition/include/st_battery.h](../Acquisition/include/st_battery.h)
- [Acquisition/include/st_cal_endpoint.h](../Acquisition/include/st_cal_endpoint.h)
- [Acquisition/include/st_cal_input.h](../Acquisition/include/st_cal_input.h)
- [Acquisition/include/st_core.h](../Acquisition/include/st_core.h)
- [Acquisition/include/st_ignition.h](../Acquisition/include/st_ignition.h)
- [Acquisition/include/st_link2.h](../Acquisition/include/st_link2.h)
- [Acquisition/include/st_link_runtime.h](../Acquisition/include/st_link_runtime.h)
- [Acquisition/include/st_radio.h](../Acquisition/include/st_radio.h)
- [Acquisition/include/st_reference.h](../Acquisition/include/st_reference.h)
- [Acquisition/include/st_session_zero.h](../Acquisition/include/st_session_zero.h)
- [Acquisition/pin_candidates.csv](../Acquisition/pin_candidates.csv)
- [Acquisition/ports/st_ignition_f4.c](../Acquisition/ports/st_ignition_f4.c)
- [Acquisition/ports/st_ignition_f4.h](../Acquisition/ports/st_ignition_f4.h)
- [Acquisition/revalidate_candidate.mx](../Acquisition/revalidate_candidate.mx)
- [Acquisition/src/st_auto_link.c](../Acquisition/src/st_auto_link.c)
- [Acquisition/src/st_battery.c](../Acquisition/src/st_battery.c)
- [Acquisition/src/st_cal_endpoint.c](../Acquisition/src/st_cal_endpoint.c)
- [Acquisition/src/st_cal_input.c](../Acquisition/src/st_cal_input.c)
- [Acquisition/src/st_frame.c](../Acquisition/src/st_frame.c)
- [Acquisition/src/st_ignition.c](../Acquisition/src/st_ignition.c)
- [Acquisition/src/st_link2.c](../Acquisition/src/st_link2.c)
- [Acquisition/src/st_link_runtime.c](../Acquisition/src/st_link_runtime.c)
- [Acquisition/src/st_logger.c](../Acquisition/src/st_logger.c)
- [Acquisition/src/st_nv.c](../Acquisition/src/st_nv.c)
- [Acquisition/src/st_radio.c](../Acquisition/src/st_radio.c)
- [Acquisition/src/st_reference.c](../Acquisition/src/st_reference.c)
- [Acquisition/src/st_session_zero.c](../Acquisition/src/st_session_zero.c)
- [Common/Inc/st_project_config.h](../Common/Inc/st_project_config.h)
- [Common/Inc/st_task.h](../Common/Inc/st_task.h)
- [Common/Src/st_task.c](../Common/Src/st_task.c)
- [Controller/.eide/eide.yml](../Controller/.eide/eide.yml)
- [Controller/SwiftThrust_Controller_Candidate/SwiftThrust_Controller_Candidate.ioc](../Controller/SwiftThrust_Controller_Candidate/SwiftThrust_Controller_Candidate.ioc)
- [Controller/board/reference_main.c](../Controller/board/reference_main.c)
- [Controller/configure_candidate.mx](../Controller/configure_candidate.mx)
- [Controller/fonts/OFL.txt](../Controller/fonts/OFL.txt)
- [Controller/fonts/SOURCE.json](../Controller/fonts/SOURCE.json)
- [Controller/include/st_cal_ui.h](../Controller/include/st_cal_ui.h)
- [Controller/include/st_ui.h](../Controller/include/st_ui.h)
- [Controller/pin_candidates.csv](../Controller/pin_candidates.csv)
- [Controller/revalidate_candidate.mx](../Controller/revalidate_candidate.mx)
- [Controller/src/st_cal_remote.c](../Controller/src/st_cal_remote.c)
- [Controller/src/st_cal_ui.c](../Controller/src/st_cal_ui.c)
- [Controller/src/st_font_subset.inc](../Controller/src/st_font_subset.inc)
- [Controller/src/st_raster.c](../Controller/src/st_raster.c)
- [Controller/src/st_ui.c](../Controller/src/st_ui.c)
- [Controller/src/st_ui2.c](../Controller/src/st_ui2.c)
- [HANDOFF.md](../HANDOFF.md)
- [Models/CALIBRATION.md](../Models/CALIBRATION.md)
- [Models/LORA_IGNITION_PROTOCOL_DRAFT.md](../Models/LORA_IGNITION_PROTOCOL_DRAFT.md)
- [Models/LORA_PROTOCOL.md](../Models/LORA_PROTOCOL.md)
- [Models/LORA_PROTOCOL_R8.md](../Models/LORA_PROTOCOL_R8.md)
- [Models/R8_DELIVERY.md](../Models/R8_DELIVERY.md)
- [Models/REFERENCE_APPLICATION.md](../Models/REFERENCE_APPLICATION.md)
- [Models/SENSOR.CFG.example](../Models/SENSOR.CFG.example)
- [Models/build_reference.py](../Models/build_reference.py)
- [Models/calibration_ui_preview.png](../Models/calibration_ui_preview.png)
- [Models/calibration_ui_preview_en.png](../Models/calibration_ui_preview_en.png)
- [Models/check_link_runtime.py](../Models/check_link_runtime.py)
- [Models/check_reference.py](../Models/check_reference.py)
- [Models/config_pending.json](../Models/config_pending.json)
- [Models/cubemx_r8_acquisition.log](../Models/cubemx_r8_acquisition.log)
- [Models/cubemx_r8_controller.log](../Models/cubemx_r8_controller.log)
- [Models/export_stlog.py](../Models/export_stlog.py)
- [Models/firmware_host_validation.log](../Models/firmware_host_validation.log)
- [Models/firmware_host_validation_en.log](../Models/firmware_host_validation_en.log)
- [Models/generate_ui_font.py](../Models/generate_ui_font.py)
- [Models/ignition_pulse_validation.log](../Models/ignition_pulse_validation.log)
- [Models/ignition_ui_preview.png](../Models/ignition_ui_preview.png)
- [Models/ignition_ui_preview_en.png](../Models/ignition_ui_preview_en.png)
- [Models/link_runtime_validation.log](../Models/link_runtime_validation.log)
- [Models/r8_pin_validation.log](../Models/r8_pin_validation.log)
- [Models/reference_arm_validation.log](../Models/reference_arm_validation.log)
- [Models/reference_host_validation.log](../Models/reference_host_validation.log)
- [Models/reference_pins.csv](../Models/reference_pins.csv)
- [Models/run_host_tests.py](../Models/run_host_tests.py)
- [Models/test_cal_integration.c](../Models/test_cal_integration.c)
- [Models/test_calibration.c](../Models/test_calibration.c)
- [Models/test_core.c](../Models/test_core.c)
- [Models/test_ignition.c](../Models/test_ignition.c)
- [Models/test_ignition_f4.c](../Models/test_ignition_f4.c)
- [Models/test_link_runtime.c](../Models/test_link_runtime.c)
- [Models/test_lowlevel.c](../Models/test_lowlevel.c)
- [Models/test_r2.c](../Models/test_r2.c)
- [Models/test_reference.c](../Models/test_reference.c)
- [Models/test_stop.c](../Models/test_stop.c)
- [Models/test_storage.c](../Models/test_storage.c)
- [Models/ui_host_preview.png](../Models/ui_host_preview.png)
- [Models/ui_host_preview_en.png](../Models/ui_host_preview_en.png)
- [Models/validate_candidates.py](../Models/validate_candidates.py)
- [PROTOCOLS.md](../PROTOCOLS.md)
- [README.md](../README.md)
