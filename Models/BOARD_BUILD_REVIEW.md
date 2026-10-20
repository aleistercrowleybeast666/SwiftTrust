# 非点火标定：整板编译可行性审查

2026-10-20。结论：已实现的组件可在两块目标MCU真实SDK/EIDE工程中编译、保留必要入口并链接；**尚无可运行整板固件，未板测**。没有用假硬件头、空diskio或成功返回的模拟HAL掩盖缺口。没有下载依赖或访问硬件，构建输出均位于自动清理的临时目录。

| 目标 | 实际编译 | Flash | RAM（含脚本heap/stack预留） | 验证 |
|---|---|---:|---:|---|
| F411RCT6 | 46C+1汇编 | 41,800B/256KiB | 52,688B/128KiB | 无未定义符号；ADC完成/错误、队列、TF参数和system入口均保留 |
| G071KBT6 | 41C+1汇编 | 37,564B/128KiB | 7,224B/32KiB | 无未定义符号；remote init/port/step与cal command均保留 |

见`eide_link_validation.log`。LINK_CHECK启用未使用段裁剪，通过显式链接根与nm符号集合检查防止新集成被裁剪。代表性对象包含F411 logger、cal system/队列、FatFs worker、capture和server；G071包含UI/remote/client/OLED。不是RTOS全部任务栈、真实IRQ最坏嵌套或最终整板RAM预算。链接脚本的512B heap、4096B stack预留不等于栈实测。

| 尚缺位置 | 确切原因 | 当前行为及可继续部分 |
|---|---|---|
| 两板`board/link_check_main.c` | 入口只做CRC/WFI；未实现SystemClock、GPIO/外设MspInit、实际IRQ路由和应用owner循环 | 保留不可烧录LINK_CHECK，避免把组件ELF称为运行固件；现有状态机和接口可继续Host验证 |
| `Models/config_pending.json` acquisition | ADC CLKIN源/Hz、power mode、OSR/PGA、传感器接线/灵敏度、激励最终值、divider/LED策略等仍未冻结 | binding/policy由调用方核实后提供；未验证则标定拒绝开始，不硬编码测试中的2.5V/128倍等合成配置 |
| `st_ads_hal`→`st_f4_capture`及`st_cal_system_init`调用处 | ADS读回、SPI owner切换、PC4 EXTI、微秒计时扩展和DMA回调还没有完整板级应用 | DMA完成代码已调用标定投递；必须在启用IRQ前绑定同一input，并由前台消费 |
| SDIO/FatFs卷owner | 需要实际SD初始化、DMA/SDIO IRQ、CD极性、mount和任务优先级 | 真实diskio/参数文件代码已编译，合成卡已测；不能代替物理卡写延迟、热插拔、断电测试 |
| 无线应用及链路NV物理后端 | 已有radio HAL和短包状态机，尚需真实DIO/TX_DONE收发循环；`st_nv`仍没有选定Flash/EEPROM介质/写入窗口 | 两端必须正确保留并顺序发送回复片段；NV未绑定拒绝建立可操作会话；TF校准文件不冒充链路NV |
| G071输入/显示实际入口 | 需要经核实的encoder定时器/按键、I2C初始化、事件循环和RF owner调度 | 仍使用既定SSD1306 128×64/0x3C；标定菜单/remote已Host覆盖，未接屏实测 |

这些是实际硬件配置/整板应用缺口，当前没有权限拒绝。继续实现需补齐真实设计或可用板测环境，不能用verified=true代替证据。点火执行、触发协议和输出链保持未实现。

Host最终43组/40,371条C断言，含真实未修改FatFs R0.12c合成卡端到端与故障注入；不会把合成卡/样本称为硬件通过。新短包官方Semtech纯计算：23B/26B在SF10/BW812500/4/5为68/74ms，SF11/BW406250/4/5为294/294ms，SF12/BW203125/4/5为1074/1175ms；四片响应需另计切换、退避和重试，不作距离承诺。
