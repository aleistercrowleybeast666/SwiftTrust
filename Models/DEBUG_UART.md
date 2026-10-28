> 历史评审记录：本文中的旧ADC、时钟及引脚假设已由[2026-10-29双板硬件契约](REFERENCE_APPLICATION.md)替代；当前测试/交付见[R8硬件适配报告](R8_HARDWARE_ADAPTATION_20261029.md)。历史结果不代表当前固件或实板验证。

# 两端独立调试UART预留

两端都采用USART1，候选115200 baud、8N1、3.3V逻辑。只经官方器件数据库/CubeMX候选校验，尚无板级绑定或串口实测。

| 项目 | TX | RX | 独立SWD |
|---|---|---|---|
| F411RCT6 LQFP64 | PA9，物理42脚，AF7 | PA10，物理43脚，AF7 | PA13/46 SWDIO，PA14/49 SWCLK |
| G071KBT6 GP非N LQFP32 | PA9，物理19脚，AF1 | PA10，物理21脚，AF1 | PA13/24 SWDIO，PA14-BOOT0/25 SWCLK |

G071的20脚是PC6，不能按连续编号把PA10误画在20脚。现有编码器PA0/PA1及按键PA2/PA3、OLED PB8/PB9、无线PB3–PB7/PA8/PA15均不冲突；也不占用F411的ADC SPI、TF、四灯和预留PA8时钟。没有复用SWD作为串口。

预留TX、RX、GND，TX接USB-TTL的RX，RX接USB-TTL的TX。需要3.3V TTL适配器，不能接RS-232电平。若与SWD合并连接器，SWDIO/SWCLK/NRST与UART_TX/UART_RX必须独立命名和走线，具体连接器和排列尚未定。可提供VTREF供适配器感测逻辑电平；**3.3V/VTREF不是外部供电入口，不连接适配器电源去反供板卡**。

软件：st_debug为单一前台所有者、512B队列、64B发送保留缓冲，单次最多128B、每秒最多256B，忙时返回并在队列满或限频时丢弃计数。HAL适配用Transmit_IT；完成/错误只在回调发布，不使用阻塞发送。F411 IRQ优先级候选14、G071候选3，低于采集/无线重要中断。ADC/DRDY ISR禁止printf/字符串格式化/等待串口。多个任务需汇聚到一个调试任务后再调用该队列，当前不是多写者队列。

RX有独立128B中断接收队列，满则丢弃，不执行采集控制命令。USART初始化和物理GPIO仅在board_verified为真时进行；当前ioc仍为未绑定候选，不代表硬件已可连接。
