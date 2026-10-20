# SwiftThrust 日志与遥测协议 · 文档修订R4

> 当前无线包无版本字段/版本位；首字节仅TYPE。12B状态、9B启停、10B ACK已完成软件实现及Host回归；板级持久化、应用接线和实物链路尚未验收。STLOG001不变，无点火触发协议。

2026-10-20｜数据采集与状态；硬件未验收。

## 先看这几点

- **日志 STLOG001：** TF上保存512字节文件头、每样本32字节记录和结束记录。保留双通道原始码、DRDY时间、序号、质量标志与必要标定元数据；CSV可从完整文件或损坏前缀恢复。
- **无线当前契约：** 无版本字段，状态12B/采集启停9B/ACK 10B；握手绑定来源、按需诊断，详见第3节。旧格式只作历史对照。
- 日志、短包及旧格式参考代码均有Host回归；两个EIDE组件目标真实编译链接通过。**没有完成无线硬件链路或整机验收。**

本文件是两个协议的统一入口。旧的`Models/STLOG_FORMAT.md`及`Models/ACQUISITION_PROTOCOL_DRAFT.md`仅保留入口链接，避免维护重复规范。

## 1. 通用记法

`u8/u16/u32/u64`为8/16/32/64位无符号整数；`i32/i64`为相应补码有符号整数。下表偏移和长度均以**字节**计。**日志和无线应用帧所有多字节字段均为小端。** ADC芯片本身的SPI帧为MSB对齐/先高位；它经解析后才按本文件格式写入，并非直接把SPI字节流原样落盘。

ADC编号：芯片CH0用于推力原始码，芯片CH1用于激励/监测原始码。物理监测网络和有效比例尚未确定时，不输出虚构电压或推力。

## 2. TF日志：STLOG001

### 2.1 文件组成与CRC

文件为`512B头 + N×32B样本 + 32B结束记录`，干净停止后执行sync和close。文件头一次写入，不在结束时回写。

文件头和每条记录各用CRC32/IEEE：反射多项式0xEDB88320、init=0xFFFFFFFF、xorout=0xFFFFFFFF。校验值自身按小端存储；测试向量ASCII `123456789`结果为0xCBF43926。

### 2.2 512B文件头

| 偏移 | 长度 | 类型 | 内容及单位 |
|---:|---:|---|---|
| 0 | 8 | ASCII | `STLOG001`，含格式版本标识 |
| 8 | 2 | u16 | 文件头长度，固定512B |
| 10 | 2 | u16 | 记录长度，固定32B |
| 12 | 4 | u32 | ADC CLKIN配置频率，Hz |
| 16 | 4 | u32 | OSR，过采样倍率，无量纲 |
| 20 | 4 | u32 | 有依据的配置采样率，mHz；2kSPS写2000000 |
| 24 | 4 | u32 | boot_epoch，启动代号；**不是Unix时间** |
| 28 | 4 | u32 | 标定profile_id，0表示未指定 |
| 32 | 4 | u32 | 标定profile_version |
| 36 | 4 | i32 | 推力通道零点原始码，count |
| 40 | 8 | i64 | 推力换算斜率，nN/count |
| 48 | 8 | i64 | 监测通道换算斜率，nV/count；0表示未知 |
| 56 | 1 | u8 | 芯片CH0 PGA倍数，例如1/2/4…128 |
| 57 | 1 | u8 | 芯片CH1 PGA倍数 |
| 58 | 1 | u8 | 传感器线数，已标定时应为4或6，未指定可0 |
| 59 | 1 | 位标志 | bit0：标定有效；bit1：合成测试数据；其余保留0 |
| 60 | 448 | 保留 | 当前全部写0 |
| 508 | 4 | u32 | 头CRC32，覆盖偏移0…507 |

采样率由调用方提供经核对的配置依据，当前不是驱动自动测得的频率；不能把requested值直接当作实测。单靠本头还不能完整重建模拟前端或校准过程：传感器型号/序列号、额定mV/V、实际激励、校准日期/参考载荷、ADC功耗模式和完整滤波配置**当前没有逐项编码**。它们需要独立profile或后续兼容扩展，不能从保留字节假定已经存在。

### 2.3 每条样本32B

| 偏移 | 长度 | 类型 | 内容及单位 |
|---:|---:|---|---|
| 0 | 4 | u32 | DRDY序号；源序号在DMA忙/丢样时也应前进 |
| 4 | 8 | u64 | DRDY边沿单调时间，µs；不是写卡/无线完成时间 |
| 12 | 4 | i32 | 原始CH0码，24位符号扩展，count |
| 16 | 4 | i32 | 原始CH1码，24位符号扩展，count |
| 20 | 2 | u16 | ADC状态字，保留位含义按ADS131M02手册 |
| 22 | 2 | 位标志 | 样本质量flags，见下表 |
| 24 | 4 | u32 | 此样本前检测到的缺样数量；无法有序判断时0xFFFFFFFF |
| 28 | 4 | u32 | 记录CRC32，覆盖本条偏移0…27 |

| flags位 | 含义 |
|---:|---|
| 0 | ADC输出CRC错误 |
| 1 | 字长、CRC模式或补位格式不符合当前12B帧要求 |
| 2 | ADC报告复位/重同步/CRC等状态故障，或双通道数据未齐 |
| 3 | 至少一个通道达到24位正/负极值，疑似饱和 |
| 4 | 序号不连续 |
| 5 | 时间戳未严格递增 |
| 6…14 | 当前保留0 |
| 15 | 结束记录标识；普通样本为0 |

时间轴以板级单调时钟为前提，尚未实现UTC同步。原始码范围-8388608…8388607；故障样本的码仍保存，并用flags标明。当前不保存ADC收到的原始CRC字，因此日志CRC只能验证文件内容完整性，不能重新独立验算原SPI帧CRC。

### 2.4 标定与CSV

标定有效时：`force_N = (raw_CH0 - zero_raw) × slope_nN_per_count / 10^9`。

监测比例已知时：`monitor_V = raw_CH1 × slope_nV_per_count / 10^9`。该比例应包含实际模拟监测链路的全部缩放，不能从未选定的电阻或候选0.4比例推测。

当前CSV导出只有在profile/版本/线数/系数和标定有效标志完整，且样本flags的bit0…3均为0时才给出推力N；未知或故障时留空。监测比例为0时监测V留空。缺样/时间标志仍随行输出。电压档位不是精确SOC。

### 2.5 结束记录与损坏恢复

最后一条的flags固定0x8000；偏移0/4为最后样本序号/时间，偏移12改作**u32环形队列丢弃总数**，偏移16改作**u32 ADC完整性及源DMA错误总数**，其余保留0，仍有同样的CRC32。无样本时序号/时间可0，不能据此当作已有有效采样。

导出工具先验头CRC，再按顺序验记录CRC；遇到坏记录即停止并报告问题，断尾保留完整前缀。完整尾记录且没有多余或坏字节时报告clean_close。sync成功与写入完成分开记录，但这不能保证SD卡内部缓存和FAT目录突然断电绝对安全。

命令：`python -B Models/export_stlog.py input.BIN output.csv`。CSV排他新建，已有同名文件不覆盖。它是通用CSV，**尚未验证UNDERLINE专用导入兼容**。

## 3. 当前无线短包契约（文档修订R4，不在包内放版本）

2026-10-20：按用户追加要求，取消R2草案的版本/类型合并字节。首字节仅为完整8位 **TYPE**，值0x01…0x0C；没有版本字段，也没有版本位。代码内部`st2_`名称仅用于区别旧实现，不在空中传输。两端必须部署同一文档修订的配套固件；未来改同一包型的语义可能无法仅靠长度识别，因此需要一起更新两端。未知包型、错误长度、CRC或未绑定epoch均拒绝。TF的STLOG001标识不改。

所有多字节应用字段均为小端。末尾CRC16采用CCITT-FALSE：poly=0x1021、init=0xFFFF、非反射、xorout=0，覆盖CRC前全部字节，低字节先发。LoRa物理CRC也保持开启。

### 3.1 常规包逐字段

| 包型 | 字段和字节数 | 总长 |
|---|---|---:|
| STATUS 0x01 | TYPE1＋link_epoch4＋status_sequence2＋flags1＋mV2＋CRC2 | **12B** |
| START 0x02 / STOP 0x03 | TYPE1＋link_epoch4＋transaction_id2＋CRC2 | **9B** |
| ACK 0x04 | TYPE1＋link_epoch4＋transaction_id2＋result_state1＋CRC2 | **10B** |

STATUS偏移：TYPE=0、epoch=1、序号=5、flags=7、mV=8、CRC=10。flags：bits0…2采集状态IDLE=0/STARTING=1/CAPTURING=2/STOPPING=3/FAULT=4；bits3…4 TF UNKNOWN=0/READY_CLOSED=1/OPEN=2/FAULT=3；bit5电压新鲜；bit6 ADC新鲜；bit7质量问题锁存摘要。锁存至服务上下文重新初始化，不能当精确错误计数。TF READY需要真实挂载/卡状态，不能仅凭logger空闲推断。

mV=0…65534为有效最近测量值，0xFFFF未知/无效。真实0mV有效；过期保留最近测量并清新鲜位。1mV编码不等于1mV准确度。板级测量仍须绑定真实分压、参考电压和年龄阈值；未知不得伪造电压/百分比。

ACK result_state：bits0…2结果ACCEPTED=0、STATE_REJECTED=1、EPOCH_REJECTED=2（保留码，当前错误epoch静默丢弃）、OLD_ID=3、CONFLICT=4、COMPLETED=5、FAILED=6；bits3…5采集状态；bits6…7 TF摘要。7为保留结果。

同epoch的序号/事务号为1…65535，严格递增、不回绕；耗尽须重新建链。当前服务只缓存最近一笔命令，客户端只允许一笔未完成事务。相同ID相同命令返回缓存结果，不重新执行；相同ID改变命令拒绝；旧ID拒绝。诊断请求有独立ID，不清除命令高水位。

ACCEPTED仅表示接受。START在日志头写入/sync并进入CAPTURING后、STOP在排空/尾记录/sync/close进入IDLE后，缓存升级并发送同ID的COMPLETED ACK；错误升级FAILED。若终态ACK丢失，原命令重试可取缓存终态。STATUS没有事务ID，**不能单独证明某次事务完成**。新HELLO会退休旧会话；若此前操作结果不明，UI应查实际状态，不自动重发成新的START。ADC新鲜度与日志进入CAPTURING分开显示。

### 3.2 来源、重启和持久化边界

12B包用4B epoch索引已配对完整UID和会话，没有完整UID/boot字段。必须先握手：

| TYPE | 字段 | 总长 |
|---|---|---:|
| HELLO 0x07 | TYPE1＋目标采集UID96 12＋遥控UID96 12＋遥控持久计数u64 8＋CRC2 | 35B |
| HELLO_REPLY 0x08 | TYPE1＋采集UID96 12＋回显计数8＋新epoch4＋CRC2 | 27B |
| COMMIT 0x09 | TYPE1＋epoch4＋计数8＋CRC2 | 15B |

客户端先持久增加计数；服务端核对配对UID、只接受大于已持久记录的新计数，并持久提交计数与从不复用的epoch后才回复。重复HELLO使用同一提议，不反复分配。COMMIT后以STATUS确认建链，随后才接受启停。服务端重启时没有活跃映射，旧短包无效；上次已持久使用的旧HELLO也拒绝。任一端重启、超时重建或序号耗尽都应发起新会话。

**已实现状态机和持久化回调失败时的拒绝行为，尚未实现实际板上Flash/EEPROM持久化后端及配对配置。** `st2_nv_port`无回调/写入失败/耗尽时建链失败；不能用RAM测试存储宣称掉电后防重放。epoch分配器还须保证所管理配对域内不重复，不能直接用未检查的随机数代替。持久化必须提交后才返回true，并解决掉电原子性/磨损；布局需随板级方案确认。

UID和CRC没有密码学认证能力。此契约用于已管理的配对域；不提供恶意发射源认证。若不准备完成持久化映射，可另行评审显式来源/boot的较长格式，不能带缺失前置条件上线12B格式。

### 3.3 按需诊断，不塞入心跳

QUERY 0x05：TYPE1＋epoch4＋request_id2＋section1＋CRC2＝10B。
REPLY 0x06：TYPE1＋epoch4＋request_id2＋section1＋fragment1＋data0…16＋CRC2＝11…27B。
fragment高4位为总片数减1、低4位为索引。

当前只实现section=1的固定23B一致快照：

| 快照偏移 | 长度 | 含义 |
|---:|---:|---|
| 0 | 4 | 实际rate_millihz；时钟依据未验证则0 |
| 4 | 4 | 最近样本序号；有效性见byte22 |
| 8 | 4 | 最近durable序号；有效性见byte22 |
| 12 | 4 | 环形队列累计丢样数 |
| 16 | 4 | ADC/源错误累计数 |
| 20 | 2 | ADC年龄ms；0xFFFF未知或超范围 |
| 22 | 1 | bit0有样本、bit1有durable、bit2时钟依据已验证、bit3合成数据 |

分片0为16B数据/总包27B，分片1为7B数据/总包18B。同epoch/request_id重复读取最近快照，原内容不变；新ID重新采快照，旧ID拒绝。客户端支持乱序、相同重复、跨ID拒绝及重复片内容冲突检查；只在收齐后标记complete。request_id独立1…65535且不回绕，耗尽换epoch。未实现其他section；采样配置、标定档案、精确电压年龄等后续再定义，当前不伪装已有。

### 3.4 节奏与超时

软件支持状态位变化立即发送，空闲5…30秒心跳（测试候选10秒），终态ACK优先。诊断快照最多两片。电压数值变化本身等心跳，避免细小噪声造成广播。`st2_server_poll`只在输出槽可用时调用；若构造后RF拒绝，要保留原帧重试。发射前测量/构造，不能把长时间排队的旧测量冒充新鲜。

连接有效窗口=`3×heartbeat_ms＋max_airtime_ms＋1000ms`。只有递增STATUS刷新年龄；ACK、旧包和重复包不刷新。电压/ADC新鲜位须由采集端按约定阈值产生，接收端结合链路年龄和最大传输时间判断；12B没有精确测量年龄，不能由一个新鲜位推断精确时间。

重试计时锚点为TX_DONE，需覆盖ACK空中时间＋切换/处理裕量。调用者注入0…250ms抖动；时钟回绕采用无符号差值。单轮重试有次数上限；收到ACCEPTED后允许继续查询同事务缓存，整个操作仍受总deadline限制。缺少TX_DONE时不会密集连发；应通知RF超时完成以进入有界恢复。心跳周期、最大空中时间、ACK_RTO、总操作时限必须与实际射频配置匹配，测试参数不等于定型参数。

无原始推力无线流；高频原始数据写TF。无线失联不自行把记录当作已停止，需重新获取真实状态。RF源码当前只接收本节包型/长度/CRC，不自动回退旧v1。


### 标定短包扩展（文档修订R4）

新增CAL_COMMAND 0x0A（23B）、CAL_QUERY 0x0B（9B）、CAL_REPLY 0x0C（四片各26B），均沿用当前epoch、非零transaction_id和CRC16，小端字段与首字节完整TYPE保持一致。没有增加版本字段或版本位，旧0x01～0x09包布局不变；旧固件会拒绝新增类型，两端须一致部署。

COMMAND偏移：TYPE@0、epoch u32@1、id u16@5、action@7、unit@8、milli_units u64@9、gravity u32@17、CRC16@21。动作0～6为BEGIN/ZERO/REFERENCE/LOAD/SAVE/CANCEL/REFERENCE_CLEAR；仅REFERENCE允许其后13B非零。QUERY与START同为9B布局。REPLY偏移：TYPE@0、epoch@1、id@5、fragment@7、16B data@8、CRC16@24；fragment为0x30～0x33，冻结64B视图定义见`Models/CALIBRATION.md`。

CAL命令/查询与START/STOP共享单调ID流。缓存最后完整请求字节和四片视图，重复SAVE只重发结果；冲突或旧ID拒绝。控制器四片完整前不宣布完成，STATUS不能完成标定操作。重试等待`ack_rto+3*max_airtime+jitter`；草稿超出operation会话租期取消，已提交结果不因租期消失。丢SAVE回复应显示结果未知，并查询实际状态/active revision。START在草稿期被锁，创建日志前重新核对并应用活动profile；采集中不允许换参数。配对UID/CRC不是密码认证；无点火动作。

TF参数文件实际保存和恢复的限制、真实卡掉电不确定性及编译证据见`Models/CALIBRATION.md`、`Models/BOARD_BUILD_REVIEW.md`。

### 3.5 空中时间与吞吐含义

使用未修改Semtech `sx128x_get_lora_time_on_air_in_ms`，16-symbol前导码、显式物理头、PHY CRC开启，应用CRC计入payload，ms向上取整：

| SF / BW Hz / CR | 9B命令 | 10B ACK | 12B状态 | 旧49B状态 |
|---|---:|---:|---:|---:|
| 10 / 812500 / 4/5 | 49ms | 55ms | 55ms | 99ms |
| 11 / 406250 / 4/5 | 193ms | 219ms | 219ms | 420ms |
| 12 / 203125 / 4/5 | 772ms | 772ms | 873ms | 1578ms |
| 12 / 203125 / 4/8 | 893ms | 893ms | 1054ms | 2183ms |

12B比49B少75.5%的应用字节，纯空中时间约少45%…52%。不能把这当作距离保证。SF11该配置一帧12B约219ms，`12/0.219≈54.8B/s`仅是孤立帧的应用载荷/占空时间比；协议并不连续发送该速率。10秒心跳平均1.2B/s状态负载；SF12/203125/4/5命令＋单次ACK纯发射就约1.544秒。切换、处理、退避和重试还会延长响应。

复现：`python -B Models/estimate_airtime.py`；结果`Models/airtime_estimates.json`。官方源[Semtech SX128x驱动](https://github.com/Lora-net/SWL2001/blob/master/lbm_lib/smtc_modem_core/radio_drivers/sx128x_driver/src/sx128x.c)，SHA256 `601b65e1b20b1f634495ab481e10d5ae9e3ca39b6b88eef46dcafdc72146a8ce`。SDK版本/无线参数/天线/供电与环境仍须实际验证。

### 3.6 旧49B构成与修订记录

旧49B=magic2＋version1＋type1＋length1＋session4＋sequence4（合计13B）＋payload34＋CRC2。payload34=boot4＋last_command4＋state1＋storage1＋voltage_flags1＋quality_flags1＋mV2＋voltage_age2＋rate4＋sample_seq4＋drop2＋ADC_errors2＋durable_seq4＋ADC_age2。下方历史附录保留旧字段偏移，仅作对照；RF当前入口拒绝旧格式。

文档R1记录旧格式；R2提出12/9/10B和握手；R3取消空中版本语义，TYPE改为0x01…0x09，并同步实现短包、握手状态机、终态ACK、心跳、诊断分片和UI。**文档修订记录不发送到无线包内。**

## 历史附录A：无线应用协议v1（旧代码基线）

### A.1 公共帧

帧长为`13B头 + payload_length + 2B CRC16`，最短15B、当前最长49B。无线模块负责包边界，当前没有额外COBS/转义层。

| 偏移 | 长度 | 类型 | 字段 |
|---:|---:|---|---|
| 0 | 2 | ASCII | `ST`，即0x53 0x54 |
| 2 | 1 | u8 | 版本，固定1 |
| 3 | 1 | u8 | 消息类型 |
| 4 | 1 | u8 | payload_length，负载字节数 |
| 5 | 4 | u32 | paired_session，事先配置的配对会话标识 |
| 9 | 4 | u32 | 命令事务ID，或独立STATUS序号 |
| 13 | 可变 | bytes | 负载，按消息类型解释 |
| 13+负载长 | 2 | u16 | CRC16，覆盖帧首至负载末 |

CRC为CRC16/CCITT-FALSE，poly=0x1021、init=0xFFFF、不反射、xorout=0。校验值小端存储。ASCII `123456789`结果为0x29B1。它检测误码，不提供认证或加密。

### A.2 消息类型和长度

| 类型 | 值 | 方向 | 负载 | 总长 |
|---|---|---|---|---:|
| START | 0x01 | 遥控→采集 | expected_boot_epoch u32 | 19B |
| STOP | 0x02 | 遥控→采集 | expected_boot_epoch u32 | 19B |
| QUERY | 0x03 | 遥控→采集 | 空 | 15B |
| HEARTBEAT | 0x04 | 遥控→采集 | 空；当前响应与QUERY一致 | 15B |
| ACK | 0x80 | 采集→遥控 | boot u32 + result u8 + observed_state u8 | 21B |
| STATUS | 0x81 | 采集→遥控 | 以下34B状态负载 | 49B |

START/STOP不带模拟增益、采样率或电源设置，也不远程修改标定。错误版本/长度/CRC、未知类型、错误session不会改变采集状态。

### A.3 ACK负载6B

| 负载偏移 | 长度 | 类型 | 含义 |
|---:|---:|---|---|
| 0 | 4 | u32 | 当前采集端boot_epoch |
| 4 | 1 | u8 | result：0接受，1状态/配置不允许，2boot不符，3旧/零ID，4同ID内容冲突 |
| 5 | 1 | u8 | 接受/拒绝时观察到的采集状态 |

ACK头中的事务ID回显请求ID。**ACK接受不等于采集已开始或已经结束落盘。** START的头文件写入并sync后才进入CAPTURING；STOP经缓存排空、尾记录、sync和close后才进入IDLE。遥控UI等新鲜实际STATUS确认。

### A.4 STATUS负载34B

以下偏移从负载首字节开始；转换为全帧偏移需加13。

| 负载偏移 | 长度 | 类型 | 内容及单位 |
|---:|---:|---|---|
| 0 | 4 | u32 | boot_epoch，非UTC时间 |
| 4 | 4 | u32 | last_processed_command_id，最后处理过的启停ID；不保证其被接受 |
| 8 | 1 | u8 | 采集状态：0 IDLE，1 STARTING，2 CAPTURING，3 STOPPING，4 FAULT |
| 9 | 1 | u8 | storage：当前产生0 CLOSED、1 OPEN、4 ERROR；不等于已确认插卡/就绪 |
| 10 | 1 | 位标志 | voltage_flags：bit0感测电路存在，bit1测量有效，其余0 |
| 11 | 1 | 位标志 | quality_flags，见下文 |
| 12 | 2 | u16 | 采集端最近测量电压，mV；需同时看有效标志 |
| 14 | 2 | u16 | 发送端电压测量年龄，ms；0xFFFF为未知/超范围 |
| 16 | 4 | u32 | 有依据的配置采样率，mHz；未知时0并置质量bit0 |
| 20 | 4 | u32 | 最近从队列消费并记录的样本序号 |
| 24 | 2 | u16 | 环形队列丢弃数，饱和至65535 |
| 26 | 2 | u16 | ADC完整性/源DMA错误数，饱和至65535 |
| 28 | 4 | u32 | 最近经sync确认的样本序号，需质量bit2有效 |
| 32 | 2 | u16 | 最近样本DRDY到构造状态时的年龄，ms；0xFFFF为未知/超范围 |

quality_flags：bit0采样率依据未知；bit1已有样本（不代表该样本一定CRC正确）；bit2已有sync确认样本；bit3至少一个u16计数发生饱和；bit4合成测试数据；其余保留0。

合法的真实0mV与未知电压通过voltage_flags区分。没有样本/同步时，序号字段的默认0不能冒充有效进度。sync确认序号不是SD块号、文件偏移或DMA完成序号。

### A.5 去重、重试、重启与过期

- 同一session/boot下，启停事务ID从1递增，不回绕0。相同ID/内容重发返回最近缓存决定，不重复启动日志；同ID改START/STOP被拒绝，更旧ID被拒绝。只缓存最近一次决定并保留高水位，QUERY不会清空它。
- 已配置session后，遥控器先QUERY取得boot和最后处理ID，接续下一ID。boot_epoch要求配对生命周期内单调增加、不复用；真正跨掉电的epoch来源和配对持久化仍待BSP完成，当前RAM逻辑不提供持久“恰好一次”。
- 重试使用原ID、原字节；射频未接受发送时不记已发送次数。ACK接受后等实际状态，ACK丢失时也可由匹配last_command及目标状态的新鲜STATUS完成事务。超时表示未确认，不伪造REC或STOP。
- STATUS头序号独立单调，以32位半范围比较接受新包。旧boot、旧/重复STATUS不会刷新状态年龄。新boot取消旧在途事务。
- 遥控器将发送端电压年龄加上本地接收后经过时间；ACK不刷新电压年龄。停止采集不会将最近实测电压清零。
- 当前client状态新鲜度阈值为3秒；重试间隔/次数是可配置参数，Host回归用1秒/3次。**没有按这些值验收真实LoRa链路。** 当前HEARTBEAT不包含掉线自动停止逻辑，采集端不会因遥控器掉线被该协议核心自动停止。

### A.6 无线层尚未定项

实际E28/SX1281收发集成、频点、SF/BW/CR、功率、空中时长、最终重试预算、配对/认证、boot持久来源均未完成验收。旧参考2473MHz、SF10/BW812kHz或其他候选不能当作已锁定当前配置。

**周期遥测频率尚未确定，也尚无周期推送调度实现。** 当前核心在QUERY或HEARTBEAT时生成STATUS；开始/停止时ACK与后续状态如何排队和定时发送仍需射频/应用层接线。不能声称现有STATUS已按1Hz或其他频率在空中发送。

无线只承载命令/状态摘要。高频推力原始数据存TF；采样率、屏幕刷新率和无线状态频率是不同参数。

## 4. 实现与证据位置

- 当前短包/事务/会话/诊断：`Acquisition/src/st_link2.c`及`include/st_link2.h`。持久化接口只是契约，板上后端未接。
- RF严格过滤：`Acquisition/src/st_radio.c`；当前UI入口：`Controller/src/st_ui2.c`；旧`st_protocol.c`仅参考回归。
- STLOG：`st_logger.c`、`st_core.c`；CSV校验/恢复：`Models/export_stlog.py`。
- 当前Host：`Models/firmware_host_validation.log`，43组/40,371条C断言及Python导出边界。2026-10-20最新源码真实双EIDE组件链接均通过：`Models/eide_link_validation.log`；旧对象报告仅历史证据。
- OLED按用户选择恢复原机械臂项目SSD1306/0x3C/128×64规格，参见`Models/OLED_REFERENCE.md`。四页面中的两个点火相关页只有静态说明及返回，无触发接口。
- 尚缺板级入口/任务调度、ADC/RF/SDIO已实现适配器的实际初始化与IRQ路由、链路NV物理后端及电气确认；标定TF后端已实现并Host验证。LINK_CHECK仍不是可运行整板固件，不能据此称硬件PASS。

记录吞吐、40KiB名义数据缓存、同步调度限制、TR_F转换及校准物理验收边界见[HANDOFF](HANDOFF.md)。本轮没有修改STLOG001布局。

当前已加入`st_nv.c`双槽96B链路记录核心及真实FatFs diskio/ADC-RF-SDIO HAL适配；无线TYPE/长度和STLOG001布局不变。实际NV介质/写入窗口、任务和板级接线未完成，见[板级接入](Models/BOARD_INTEGRATION.md)。

标定使用256B内部参数记录STCAL001，现已接入TF双文件后端、ADC只读队列和非点火短包；新增TYPE 0x0A/0x0B/0x0C，旧包布局与STLOG001保持不变，见[标定说明](Models/CALIBRATION.md)。

文档R4补充非点火标定0x0A～0x0C及双文件参数保存；文档修订号仍不进入无线包。
