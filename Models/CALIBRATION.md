# 采集标定核心与单编码器菜单

本阶段已接入只读ADC DMA样本队列、真实FatFs双文件参数后端和配对会话内的非点火短包交互。标定基线e6589b0的Host **43组/40,371条C断言**通过，含端到端合成卡故障注入；双板真实SDK/EIDE组件链接通过并检查了入口保留。**未连接真实ADC、TF卡、射频或板卡；当前入口仍为LINK_CHECK，完整板级初始化尚未完成。**点火页仍为静态说明和返回。

## 一个编码器即可操作

仅采集页增加“CALIBRATION”入口。旋转选择、短按进入；各确认页默认返回。

1. 开始后确认卸去载荷，选择CAPTURE ZERO，采集稳定空载窗口。
2. 参考输入可选常用质量：100g、200g、500g、1kg、2kg、5kg、10kg。列表默认返回，不自动应用任何值；这些是便利输入，不能保证手中砝码的真实重量，需核实或输入实际值。
3. 自定义先选g/kg/N。数值为6位整数＋3位小数，旋转选择数字位，短按进入EDIT DIGIT，旋转逐次0…9循环，短按确认回SELECT DIGIT。当前数字位以单色反色显示。确定、返回也是旋转可选项，不需要新增按钮，不依赖长按、彩色或转速加速。
4. 切换单位明确清空待输入数值与未应用的参考值；屏幕提示VALUE CLEAR。不会把1.234kg默默解释为1.234g。零值、整数溢出、传感器额定力以上的参考值拒绝；不会截断或自动夹到上限。
5. 最终确认页显示完整值＋单位；质量输入显示标准重力约定。确认参考后，再确认施加已知载荷、采集加载窗口。
6. 审阅零点与nN/count系数，SAVE NEW成功后才替换活动参数；取消、错误、超时、保存失败保持旧活动profile。未绑定保存后端显示SAVE UNBOUND并拒绝保存，不假装成功。

固定三位小数仅为输入分辨率：g步进0.001g、kg步进0.001kg、N步进0.001N，**不是砝码或测量准确度声明**。编辑器数值范围0…999999.999，实际允许载荷还受绑定的传感器额定力约束。可选BACK输入事件仅兼容已有按键；全流程测试只用旋转/短按也可进出、确认和取消。

## 单位、模型和绑定

`Acquisition/src/st_cal.c`使用整数及显式单位。质量按保存的`gravity_um_s2`换算，菜单采用`g0=9.80665m/s2`（9806650µm/s²），不是推断现场重力。N为直接力输入，gravity=0。质量输入以mg计算，`F_nN=round(mass_mg×gravity_um_s2/1000)`。已知质量不等于无误差已知力，需核实标准质量、重力约定及安装方向。

稳定窗口只读取原始24bit ADC，不修改capture或STLOG记录。`r0`和`r1`为窗口均值、按最近整数count取整；`k=round(Fref_nN/(r1-r0))`，保存为有符号nN/count。允许因接线/方向得到负斜率，审阅时显示符号。零点和系数的量化分辨率不等于传感器精度。系数低于该编码分辨率而四舍五入为0时拒绝，不生成零力值伪标定。

绑定包含profile ID、sensor ID、config ID、ADC时钟/OSR/速率、两路PGA、线数、功耗模式、滤波ID、配置激励电压及额定力。任一项改变后旧参数不能直接应用；未知/未核验配置不能开始。ID须由应用明确管理，普通4/6线桥式传感器不会被本代码自动识别。激励字段是配置标称值，不冒充实时测量；本轮不增加实时激励比率补偿。

窗口样本数、稳定范围、最小跨度、settle、采样超时、零点有效期均由policy输入并记录，没有替硬件默认选择阈值。坏CRC/格式/状态、饱和、不连续序号或时间、噪声超限、配置变化、零点过期都拒绝候选。静态标定不证明动态带宽或冲击响应，仍需实际加载/卸载与多点复核。

## 参数替换和原始数据

候选与活动profile分离。保存前核对活动profile指纹及当前配置，防止覆盖已被其他操作更新的参数。`st_cal_commit`只在SAVE回调报告成功后更新活动profile；无回调或失败时旧参数保留。save回调必须在唯一owner上下文完成真实提交/验证，不能将“已排队”或“收到无线ACK”报告为保存成功。物理后端的掉电原子性和不确定结果处理仍待实现与实测。

参数提供独立256B小端记录`STCAL001`和CRC32，保存单位、输入值、重力约定、Fref、零点/加载均值、跨度统计、计数、policy、配置绑定、修订及单调时间。单调时间不是UTC；本轮未保存全部标定窗口的逐样本波形到TF。历史采集文件的双通道原始ADC、时间、质量标志保持不变，可配合同文件头零点/系数复算。编码/解码逐字节损坏测试通过。

`st_cal_apply_logger`仅在IDLE、无在途文件操作/采集时应用已接受profile。绑定或真实/合成来源不匹配时取消calibration_valid，保留旧系数供诊断，不把不匹配参数当成有效力。已挂接标定端点的无线START会在创建日志前执行该核验，草稿期拒绝START。STLOG001仍为512B头/32B记录；新增非点火标定TYPE，已有无线包布局不变。


## 当前接入链与所有权

- `ports/st_f4_capture.c:st_f4_capture_complete`复制真实DMA接收缓冲后，将同一`st_capture`只读投递到`st_cal_input`。标定和日志使用独立队列；没有修改双通道原始码、DRDY时刻或序号。DMA忙/启动失败/错误回调通知标定候选失败。
- `st_cal_input`固定64项/2048B，单IRQ生产者、单前台消费者，一步最多处理64项；溢出或源错误拒绝候选。开始窗口前仍在途的旧帧按原始边沿时间丢弃。只在ZERO/LOAD采样窗口使能；2kSPS时容量32ms，前台服务需留出充分余量，实际调度/IRQ延迟尚未板测。
- `ports/st_cal_system.c:st_cal_system_init`在卷已mount、logger/worker初始化后，读取参数、绑定服务/端点并挂到`st2_server`。调用方仍须提供**实际核实的**binding、policy及与DRDY相同的扩展64bit微秒时钟；不提供时保持未配置，绝不代填硬件参数。
- ADC的EXTI、SPI DMA完成/错误路由及配置阶段/采样阶段SPI所有权仍需完整板级入口接线。启用EXTI前调用`st_f4_capture_bind_calibration(&capture, &cal.endpoint.input)`；前台周期调用`st_cal_endpoint_step(&cal.endpoint)`。此绑定不初始化硬件。
- 控制器调用`st_cal_remote_init`、`st_cal_remote_port`绑定既有`st_cal_ui`，仅当标定菜单可见时调用`st_cal_remote_step`。`st_link_client_step`负责既有`tx_due/accepted/tx_done/receive`与driver tick；板级owner仍须初始化实际硬件。菜单背景查询复用已确认缓存，不阻塞数字位编辑；用户动作可取代只读查询。未确认的新命令不能由旧的SAVED缓存完成。
- 服务、配置、logger命令和FatFs卷由一个串行前台owner调度。SAVE可在这个owner执行有界块IO的同步文件调用；**不在DRDY/RF ISR中写文件**，不能让两个任务并发操作非重入FatFs。保存时无活动采样窗口，真实文件系统总体时延仍待板测。RF IRQ只置标志；回复输出槽被占时必须保留已有包并依次重试，不能覆盖未发出的片段。

## TF参数文件与恢复

`ports/st_cal_fatfs.c`使用`CAL0.BIN`、`CAL1.BIN`两个固定名称、各1024B的文件，按需创建；2KiB是两文件均存在时的内容，不包含文件系统分配开销。每项前512B为`STCS0001`、u64代号、256B `STCAL001`及CRC32；后512B为`STCC0001`提交封记、相同代号、数据CRC及封记CRC。这些是内部磁盘记录标识，不是无线版本。

写入非活动槽的数据扇区→f_sync→提交封记扇区→f_sync→f_close→重新打开并逐字节读回比较。当前接受槽不被写入，不用rename/unlink/mkfs。代号/修订拒绝回绕；陈旧修订不能覆盖较新的盘上记录。核心仅在整个回调成功后替换RAM活动profile，采集中、logger在途或卷worker非空闲时拒绝保存。

启动读取两槽，验证长度、数据/封记CRC、profile语义和完整配置绑定，选择最高有效代号。有效旧槽可与撕裂的新槽共存；两个槽均损坏、相同代号冲突或遇到异传感器/配置则明确报错，不自动格式化/覆盖。无文件显示未校准。当前只支持一个配置的双槽记录，尚无多传感器参数库浏览/迁移功能。

运行I/O失败锁存磁盘故障，拒绝继续保存，RAM旧活动profile保留；需显式恢复块设备/重新mount并重开参数来核对盘上实际结果。最后一次sync/读回失败时，新项可能已经写入卡，重启后可能选择它，因此不声称物理上必定回滚。FAT共享元数据、SD卡FTL/缓存和掉电行为不能由双文件/CRC保证，仍需真实卡断电测试与硬件供电验证。取消发生在SAVE之前不会写参数；SAVE已经完成后取消不回滚接受过的参数。

## 有界短包与界面结果

协议文档修订R4新增TYPE 0x0A/0x0B/0x0C，仍没有无线版本字段/版本位。原有0x01～0x09及STLOG001布局不变，最大包仍35B，两端应部署同一修订。

| 包 | 总长 | 作用 |
|---|---:|---|
| CAL_COMMAND 0x0A | 23B | epoch4/id2后为action1/unit1/milli_units8/gravity4，最后CRC2 |
| CAL_QUERY 0x0B | 9B | epoch/id查询实际标定状态 |
| CAL_REPLY 0x0C | 每片26B，共4片 | epoch/id、fragment1、16B冻结快照、CRC2；片号0x30～0x33 |

动作0～6依次BEGIN、ZERO、REFERENCE、LOAD、SAVE、CANCEL、REFERENCE_CLEAR；非REFERENCE动作的其余13B必须为0。命令/标定查询与START/STOP共享严格递增的非零transaction_id。相同ID和相同请求仅重发冻结快照，SAVE不会重复落盘；冲突、旧ID、旧epoch、坏CRC和长度拒绝。查询会消耗新的transaction_id，因此控制器在在途动作结束/超时前不会发另一个动作。

64B快照布局：state@0、error@1、flags@2、request_failed@3；samples/required/candidate_revision/active_revision@4/8/12/16；zero/load i32@20/24；slope_nN和Fref i64@28/36；unit@44、milli_units u64@45、gravity u32@53；57～63保留0。flags依次configured、storage_bound、idle、simulated、have_reference。只有同epoch/id四片完整重组才更新已确认视图；不混用不同查询。

重试仍以TX_DONE为锚，标定回复窗口为`ack_rto_ms + 3*max_airtime_ms + jitter`，总operation deadline/次数有界。后台最多每heartbeat查询一次；仅标定菜单可见时进行。会话失联超过operation_ms只取消未完成草稿并释放START锁；已提交结果保留供晚到查询核实。配对UID/CRC不能防御主动伪造发射端，不称为密码认证。

SAVE在途显示WAIT RESPONSE；丢回复超时显示RESULT UNKNOWN和CHECK LINK STATUS，不能谎报保存成功或旧值必定未改。稍后完整查询返回真实状态及活动修订；已确认保存才显示CALIBRATION SAVED。

## 验证与剩余边界

Host涵盖ADC队列溢出/错误/序号回绕/旧边沿、冻结四片乱序与重复、冲突与CRC/截断、SAVE仅执行一次、START锁/配置失效、菜单查询响应性、失联结果核对；真实FatFs/diskio合成FAT16在每个扇区提交处注入失败及0/17/511B部分写，核对旧活动文件字节不变，区分提交但报错的不确定情况。另有短包→DMA队列→服务→真实FatFs→活动参数→日志头元数据→重挂载恢复的完整Host路径。

具体数量、源文件哈希和双板链接见`firmware_host_validation.log`、`eide_link_validation.log`；板级可行性见`BOARD_BUILD_REVIEW.md`。真实ADC/TF/RF、供电、调度与整板运行未测，不能据此宣称板测通过。校准窗口仍只持久化统计与系数，没有保存每个窗口的全部原始样本；历史实验STLOG的原始数据保留不变。静态砝码校准不证明动态带宽。


本文43组/40,371断言对应e6589b0标定基线；后续无线owner接线单独定向验证6组/313断言。当前依赖清单见`BOARD_DECISIONS.md`。
