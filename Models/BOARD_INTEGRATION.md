> 历史评审记录：本文中的旧ADC、时钟及引脚假设已由[2026-10-29双板硬件契约](REFERENCE_APPLICATION.md)替代；当前测试/交付见[R8硬件适配报告](R8_HARDWARE_ADAPTATION_20261029.md)。历史结果不代表当前固件或实板验证。

> 2026-10-20 更新：下文是 b539928d 之前的组件阶段记录，已由实际双板 ReferenceApp 替代；当前入口、参考参数、NV与接线以 [REFERENCE_APPLICATION.md](REFERENCE_APPLICATION.md) 为准。未上板验证仍然成立。

# 板级接入：当前依赖和可编译范围

本阶段完成真实HAL传输适配和可移植持久化记录核心，移除`board/link_check_diskio.c`占位。两端EIDE仍为LINK_CHECK：入口CRC自检/WFI，不初始化外设；没有可直接烧录运行的整板目标。所有verified条件必须来自实际配置核对，不得为了通过bind而硬填true。

## 依赖与当前进度

| 部分 | 本阶段实现 | 仍需提供/核验 |
|---|---|---|
| 板级入口 | 保留明确的非运行组件构建，不引入伪造成功的初始化 | ADC CLKIN来源/频率，GPIO初态/极性，SPI/SDIO/I2C/timer/DMA初始化，IRQ路由、调度与错误恢复策略 |
| ADC配置 | `ports/st_ads_hal.c`：SPI1 mode1/8bit DMA，CS、SYNC/RESET、保留在途内存、超时abort/锁存 | PA5/6/7、PA4、PC5的真实接线和模式；SPI DMA byte配置；ADS模拟/时钟/PGA/OSR参数，启动期独占SPI |
| ADC连续捕获 | 既有`st_f4_capture`读DRDY，即使不记录也排空；有generation隔离 | PC4 EXTI、64bit时间扩展、DMA回调与前台start/stop的序列化；不得在中断仍活动时重置logger |
| 射频 | `ports/st_radio_hal.c`：两板真实HAL SPI mode0/8bit命令、BUSY和复位、CS出错释放、任务上下文检查 | 两板候选SPI/CS/BUSY/RESET/DIO，时钟和SCLK时序、reset低/稳定等待、HAL tick与有界yield；LoRa最终频点/功率/参数 |
| SDIO/FatFs | `st_block.c`＋`ports/st_diskio.c`＋`ports/st_sdio_hal.c`：实际DMA扇区传输及FAT接口，512B对齐bounce、请求总超时、错误/拔卡锁存、真实sync等待 | 已完成HAL_SD_Init的卡、SDIO/DMA/IRQ、卡几何，PB5检测极性、存储任务。disk_initialize仅验证已初始化设备，不擅自开电源/重初始化/格式化 |
| 链路参数持久化 | `st_nv.c`：双槽96B记录、CRC/身份/计数、显式空白provision、读回、损坏拒绝；client/server初始化恢复高水位 | 两个独立可更新存储区域、Flash几何/保留区或其他介质驱动、提交与掉电语义、磨损/写入时机；没有选择Flash地址或实现Flash擦写后端 |
| 校准/通用参数库 | `st_cal_input`真实DMA只读队列；`ports/st_cal_fatfs.c`双文件sync/close/readback；`st_cal_system_init`恢复/绑定；`st_cal_remote`短包交互；Host端到端及ARM入口保留验证 | 实际ADC/TF/RF、板级owner/IRQ、时钟/完整binding/policy核实，物理卡掉电仍待测；见CALIBRATION.md及BOARD_BUILD_REVIEW.md |

## 实际端口的接入顺序

采集板候选：ADS SPI1 PA5/6/7、CS PA4、SYNC/RESET PC5、DRDY PC4；RF SPI2 PB13/14/15、CS PB12、BUSY PC6、DIO1 PC7、RESET PB2；SDIO PC8…PC12/PD2、CD PB5。遥控板RF SPI1 PB3/4/5、CS PA15、BUSY PB7、DIO1 PB6、RESET PA8。候选表/IOC未被当成投板冻结。

1. 板级代码先核验电源、GPIO初态和时钟，再初始化实际HAL handles。SPI速度、ADC时钟、检测极性等保持配置输入；适配器不代替MspInit，不配置未核实引脚。
2. ADC配置阶段只给`st_ads`所有权，路由HAL_SPI_TxRxCpltCallback到`st_ads_hal_complete`、错误到`st_ads_hal_error`；完成寄存器读回、启动排空且`st_ads_hal_idle`后，才转交`st_f4_capture`。启用DRDY前建立时间源和SPI/DMA回调。
3. 采集阶段PC4 DRDY ISR保存边沿时间并调用`st_f4_capture_drdy`；相应SPI DMA完成/错误只路由给当前capture owner。配置与采集不能同时拥有SPI1；必须串行化IRQ和logger重置。
4. 射频命令仅在任务里调用；DIO1 ISR只调用`st_radio_irq_isr`。SPI阻塞HAL路径要求SysTick运行、没有IRQ屏蔽，等待时允许ADC抢占。BUSY/复位等待既检查时间又有迭代上限；HAL自身仍依赖正常tick，不能承诺在任意坏中断配置下的硬实时上界。
5. SD卡初始化完成后，用真实`hsd.SdCard.LogBlockNbr`与512B逻辑扇区建立`st_block_device`，`st_diskio_bind`绑定volume0，再由唯一存储任务mount。HAL_SD_RxCplt/TxCplt/ErrorCallback分别路由到`st_sdio_hal_*`。DMA及SDIO IRQ仍需实际handler；示例传输不会替调用方猜中断优先级。
6. 块设备每次DMA一扇区、复用512B bounce，支持不对齐FatFs缓冲；大请求逐扇区处理，最多128扇区且受请求总timeout约束。GET_BLOCK_SIZE没有已验证擦除几何时返回PARERR；不凭空报告1。移除、超时或I/O故障锁存；恢复须重新核验卡并显式rebind，不自动热插拔恢复。
7. `st_logger_service`与阻塞的`st_fatfs_worker_step`应在独立执行上下文，worker优先级低于ADC及ring服务。单个while里串行调用两者不等于已解决写卡停顿。当前未接入RTOS/完整应用调度，不把组件链接称为整板固件。

## 双槽链路身份记录

这是内部存储记录，非无线包版本或校准文件。每槽96B、小端：magic STNV0001[0:8]；generation u64@8；own UID96@16；peer UID96@28；controller counter u64@40；last peer counter u64@48；epoch u32@56；epoch上界u32@60；role u8@64；epoch起点u32@65；69…91保留0；CRC32@92覆盖前92B。

storage.read/write由平台提供。write只能修改指定槽，true须表示持久提交已完成，核心还会读回逐字节核对；不能把异步“已排队”当true。启动必须`st_nv_open`并恢复记录；只有两个槽均全FF且显式`st_nv_provision`才建立初始状态。配对UID、角色或预分配epoch范围不匹配拒绝使用。两个有效槽选较新代；任何已编程但损坏的槽都拒绝自动回退，可能需要人工恢复，防止不明情况下复用历史计数。CRC不是安全认证。

若写入完整但后端返回错误，下一次open能发现已推进的计数并跳过；若写到一半，返回DAMAGED而不自动重置。Host用内存模型测试所有97个切断位置，**不能据此证明Flash或TF的真实掉电持久性**。

Flash尤其需要先决定保留区、擦除粒度、与升级程序的关系、磨损和可写窗口。单bank擦除/写入可能让取指停顿，不能在高速采集中直接调用而声称不影响ADC；须另行设计启动预留或安全异步策略。采集板TF持久化也不能由radio任务与FatFs worker并发访问同卷。控制器没有TF，需要自己的物理后端。以上策略未确定，因此本阶段不写入任意Flash地址。

## 离线证据

`python -B Models/run_host_tests.py`需HOST_CC、SDK_ROOT；采用真实FatFs R0.12c和本项目ffconf，不使用伪SDK头。合成4MiB FAT16只在测试进程RAM中创建：真实logger→worker→FatFs→diskio→块设备模型完成1500样本、文件尺寸、每记录CRC和END检查；测试后释放，没有创建D盘卡镜像。

标定基线e6589b0的Host总计43组/40,371条C断言。块设备检查非对齐缓冲、超时/时间回绕、拔卡、停止tick、错误锁存与几何；NV检查身份、范围、重开及切断位置。HAL端口本身仅通过真实SDK ARM编译链接，尚无物理总线或DMA实测。F41147C＋1汇编、G07142C＋1汇编均链接，无未解析符号。无额外SDK下载，复用已有共享缓存，构建产物仅临时目录。

标定集成现已具备`st_cal_system_init`、`st_f4_capture_bind_calibration`、`st_cal_endpoint_step`和控制器remote/port/step明确入口；它们不代替上述板级初始化。EIDE链接额外检查DMA完成/错误路径、TF与remote入口均保留。


本轮已增加`st_link_runtime`无线收发应用接线：忙时保留回复、真实TX_DONE/timeout重试锚、撤销未发射的失效请求。新增定向验证6组/313断言通过；既有43组标定报告为e6589b0基线，未重复运行。完整运行时序及四组最小外部输入见[BOARD_DECISIONS](BOARD_DECISIONS.md)。
