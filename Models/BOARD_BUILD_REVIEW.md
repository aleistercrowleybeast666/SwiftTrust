# 双板 ReferenceApp 构建审查

2026-10-20。已从 b539928d 的组件CRC/WFI入口推进到两板实际参考应用。实际参考接线、参数、首次配对和升级边界见 [REFERENCE_APPLICATION](REFERENCE_APPLICATION.md)。板子未完成，没有连接或烧录硬件，以下是离线证据。

| 实际EIDE目标 | 构建清单 | Flash加载区占用 | RAM占用，含512B heap+4096B stack预留 |
|---|---|---:|---:|
| F411RCT6 ReferenceApp | 53C+1汇编 | 向量408B + 应用57,796B；应用区192KiB | 55,176B / 128KiB |
| G071KBT6 ReferenceApp | 48C+1汇编 | 48,264B / 124KiB应用区 | 9,728B / 32KiB |

`reference_arm_validation.log`由实际EIDE unify_builder产生：两板无未定义符号，功能main、SystemClock/MSP/IRQ为强定义并保留；不是通过强制保留组件符号伪造应用。加载区不覆盖F411 sector2/3或G071 page62/63；155个外部官方SDK文件与仓库清单SHA256一致。日志同时记录实际源码、头文件、项目配置和链接脚本SHA256。静态RAM预算不等于实测栈余量/IRQ最坏嵌套；吞吐和卡延迟也未上板测量。

已实现：F411 HSI/PLL84MHz、SDIO48MHz域/TIM5、SPI1 ADC DMA/DRDY、SPI2 E28、SDIO DMA、VREFINT/四灯、UART；G071 HSI16、SPI1 E28、I2C SSD1306、TIM2编码器/按钮、VREFINT、UART；各自真实HAL MSP/IRQ和完成/错误回调。主循环接通ADC配置读回、捕获、TF worker协作等待、校准端点和无线server；遥控端接通UI、标定remote、无线client和显式CONNECT。参考参数可替换，未知传感器保持raw-only，电压仅MCU VDDA。

已实现片内Flash两区日志与空闲写入门控、本地INFO/STATUS/PAIR、掉电/CRC故障拒绝回退。重连正在采集的已配对设备时先STOP/排空，再提交新epoch；不会重放START。故障TF不自动格式化/覆盖恢复。旧LinkCheck仍为CRC/WFI、布局不保留应用NV区域，不能作为已配对设备升级固件。

离线验证：

- `firmware_host_validation.log`：45组/54,109个C断言，Python导出/坏CRC/截断前缀恢复与独占输出检查；包括3000条2kHz合成ADC在真实FatFs/diskio扇区等待期间继续处理、无丢样、正常STOP收尾。
- `link_runtime_validation.log`：6组/313断言，保持实际发送完成/超时锚定重试、失效未发送请求撤销、无自动连接或START。
- `reference_host_validation.log`：5组/75,564检查，128次重启计数、256个写入中断点、97个整理中断点、配对白名单/损坏历史/参考参数。
- `validate_candidates.py`：23项通过，准确封装/引脚/DMA/候选数学检查和新参考保留引脚；不会把参考配置写成实物已验证。
- `ui_host_preview.png`：真实C渲染的合成界面，STATUS可见BACK/CONNECT；点火页静态NOT IMPLEMENTED。

交付产物仅位于忽略的 `dist/Acquisition`、`dist/Controller`：各一份稀疏HEX与含符号ELF，`dist/manifest.json`记录大小和SHA256。临时对象已自动删除，没有完整SDK副本/RTOS依赖/额外checkout；没有跨越NV区的BIN，也没有自动烧录动作。

剩余验证需要实物板：接线/电平/时钟、DRDY/SPI/DMA时序、TF最坏延迟/拔卡/掉电、真实载荷标定、OLED/编码器/VDDA与RF丢包/距离。用户以后自行上板或另行授权测试。这些限制不是缺少main或NV实现；没有实施任何点火执行、消息或GPIO输出。

首次START与取消/再启动专项修复见 [FIRST_START_REVIEW](FIRST_START_REVIEW.md)：以共享的实际应用准备函数、真实校准端点和START协议生成STLOG头，旧顺序已观察到失败，修正后首次即应用已有系数；同时修复取消START回到IDLE仍停留ACCEPTED的问题。没有新增自动START或放宽射频命令串行规则。
