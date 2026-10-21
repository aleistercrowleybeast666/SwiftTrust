# SwiftThrust R8 完整无线协议与操作流程

2026-10-22。以 `st_frame.c`、`st_link2.c`、`st_ignition.c` 和两端 ReferenceApp 源码为准。R7 文档仅作历史资料。尚未烧录或实物验收。

## 1. 配置与身份

两端共用 `Common/Inc/st_project_config.h`。保持 2473MHz、+12dBm、SF10、BW812500Hz、CR4/5、sync 0x12、16 符号前导、显式物理头、normal IQ、LoRa CRC、高灵敏度 LNA、AGC。TX 超时500ms。仅同频不足以连接，调制、Pair ID、协议版本也必须匹配。

| 名称 | 位数 | 来源与用途 |
|---|---:|---|
| Pair ID | 32 | 每套设备编译一致，默认0x53540001；不同套必须不同 |
| MCU UID | 96 | STM32固有UID；首次绑定后保存在原NV日志，拒绝其他UID接管 |
| Session ID | 32 | 采集端持久化分配的新会话代号，非零、不复用 |
| Transaction ID | 32 | 同会话的采集、传感器、标定、诊断、ARM/FIRE/DISARM/QUERY共用递增序列 |
| STATUS序号 | 32 | 单独递增，不消耗操作ID |

握手另沿用NV的64位防回退计数，属于持久化实现细节，不代替Session ID或Transaction ID。没有使用悬空ADC产生身份。ID到上限不回绕。两端保存的UID、NV计数、实际标定参数不放入配置头。

现有固定项目CMAC校验继续用于发现和输出请求；公开源码中的项目密钥用于降低偶发误包风险，不提供抵抗恶意仿造的秘密。无需操作员配钥。普通采集包使用Pair/会话/事务与CRC校验。

## 2. 唯一基础帧

每个LoRa包只装一个应用帧，最多64B。所有整数小端，UID/tag为原始字节。

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 1 | marker=0xD8 |
| 1 | 1 | version=8 |
| 2 | 1 | kind：HELLO1、HELLO_ACK2、STATUS3、COMMAND4、RESULT5、QUERY6、DATA7 |
| 3 | 1 | code，见下表 |
| 4 | 4 | Pair ID，非零 |
| 8 | 4 | Session ID；初始HELLO可0 |
| 12 | 4 | Transaction ID；握手为0，STATUS及其收条使用状态序号 |
| 16 | 可变 | body |
| 最后2B | 2 | CRC16/CCITT-FALSE，覆盖此前所有字节；poly1021/initFFFF/refin=false/xorout0 |

检查marker、版本、精确长度、CRC和字段后，接收端再验证Pair ID、UID/Session和事务。非握手帧要求非零Session及ID。不兼容版本、R7旧帧、错误Pair/CRC/会话均明确走拒收路径且不执行操作；不向未知旧格式回包，避免把错误格式继续解释为命令。内部 `ST2_*` 操作名不是空中TYPE值。

| kind/code | body（从0起） | 总长B | 方向与触发 |
|---|---|---:|---|
| HELLO/0 | 目标UID12、发送UID12、counter:u64 | 50 | 遥控开始/恢复握手 |
| HELLO/1 | counter:u64 | 26 | 遥控确认HELLO_ACK，提交会话 |
| HELLO/2 | 发送UID12、CMAC16 | 46 | 未绑定遥控每2秒发现 |
| HELLO_ACK/0 | 采集UID12、回显counter:u64、最高事务ID:u32、flags:u8 | 43 | 响应HELLO；flags0新会话、1要求新计数（Session=0）、2恢复原会话 |
| HELLO_ACK/2 | 采集UID12、CMAC16 | 46 | 响应发现，绑定UID后停止发现 |
| STATUS/0 | battery_mV:u16、task:u8、mode:u8、flags:u8、errors:u8 | 24 | 采集周期2秒，或采集/TF/质量标志改变 |
| COMMAND/1 | mode:u8（1采集、2组合；0按采集处理） | 19 | 经过确认的START |
| COMMAND/2 | mode:u8 | 19 | STOP，包括START待确认期间 |
| COMMAND/3 | action:u8、unit:u8、milli_units:u64、gravity:u32 | 32 | 完整标定动作 |
| COMMAND/4 | action:u8、enabled:u8 | 20 | action7本次置零，8设置自动置零；enabled为0/1 |
| RESULT/0 | result_state:u8、task:u8、mode:u8 | 21 | 普通命令的接受、拒绝或完成结果 |
| RESULT/7F | 无 | 18 | 遥控收到新STATUS后发收条；ID为状态序号 |
| QUERY/1 | 无 | 18 | 请求采集诊断快照 |
| QUERY/2 | 无 | 18 | 请求完整标定视图 |
| QUERY/4 | 无 | 18 | 请求传感器/本次置零视图 |
| QUERY/5 | 无 | 18 | 恢复会话后首先请求任务快照 |
| DATA/1 | fragment:u8、data16或data7 | 35/26 | 两片诊断快照 |
| DATA/2 | fragment:u8、data16 | 35 | 四片标定视图 |
| DATA/4 | zero_state:u8、auto:u8、ready:u8、保留0:u8、mean:i32、samples:u32 | 30 | 响应传感器QUERY |
| DATA/5 | task:u8、mode:u8、output_state:u8、output_started:u8、start_id:u32、stop_id:u32 | 30 | 响应任务QUERY，恢复实际任务 |
| COMMAND或QUERY/20 | 安全操作body8、CMAC16 | 42 | ARM/FIRE/DISARM或输出状态查询 |
| RESULT或DATA/20 | 安全回复body10、CMAC16 | 44 | 安全命令结果或查询数据 |

没有增加第八类逻辑消息。HELLO/1是原有持久化握手的提交步骤；RESULT/7F是状态收条，用于采集端判断反向链路是否仍可达。收条不完成操作，也不占事务ID。

发现CMAC覆盖 `STDISC8`(7B)+帧前28B+目标UID12；发现目标为全零，响应目标为遥控UID。输出CMAC覆盖 `STIGN8`(6B)+方向1B（请求0/回复1）+采集UID12+遥控UID12+帧前24B（请求）或26B（回复）。标签后再计算应用CRC。

## 3. 会话与事务规则

首次使用原NV区域空白时，双方发现并保存对端UID；损坏或已有其他UID的NV不自动抹除。随后遥控发送HELLO。采集端无活动会话时，如计数未递增，回复flags1；遥控持久化递增计数后再发HELLO。采集端仅在日志IDLE时持久化分配新Session，回复HELLO_ACK；遥控发HELLO/1，采集端回STATUS。重复握手不会重复分配。

已有活动会话时，无论普通丢包还是遥控重启，优先回复原Session、事务高水位和flags2，不擦写Flash、不停止采集、不重开TF文件。提交后遥控发QUERY/5，恢复任务和事务序号，再允许新操作。不会因握手、STATUS、菜单返回或重连自动发送START。

恢复尚未完成时遥控禁止新START、置零、标定和ARM确认，STOP仍可表达中止意图。旧STATUS序号在重连期间也不能刷新状态；若迟到握手给出的事务高水位过旧，恢复查询得到OLD/CONFLICT后重新握手，不逐个试探旧ID。

采集端重启后旧Session不再有效，需要新Session。此前有历史时任务为UNKNOWN；无法证明原操作结果，不自动START或FIRE。采集模式可由明确STOP完成安全收尾/状态确认后开始新任务。输出结果UNKNOWN不得自动重放。

普通请求在TX_DONE后开始500ms回复等待（加0～250ms抖动），每轮最多4次，整个普通事务120秒封顶；标定四片视图额外留三片空中时间。FIRE最多上空一次，丢失回复转UNKNOWN，通过新QUERY查询原ID，不能重发FIRE来试探。DISARM可以抢占未完成的输出请求。

接收方缓存最近8个基础事务的原始请求、结果和查询快照；相同ID相同内容读取缓存，不再执行。相同ID不同内容返回CONFLICT，更老且已离开缓存窗口的ID返回OLD，仍不执行。查询、置零、标定共用高水位；迟到回复必须匹配当前Session、请求类型和ID。输出侧另外保留ARM/FIRE/DISARM各自结果，记录有限输出终态。

STATUS只更新遥测和链路年龄，不能完成任何事务。RESULT也不刷新STATUS年龄。旧STATUS序号、重复收条不能刷新链路年龄。约4秒慢闪、7秒快闪，正常常亮；驱动故障用每秒两次短闪区别于丢包；尚未收到有效状态时灭。采集端依靠状态收条判定，遥控依靠STATUS判定。未收到状态或失联不自动停止本地采集；ADC/TF真实故障仍触发原有保护和收尾。

## 4. 状态与结果

mode：0尚未有模式、1仅采集、2采集与点火、3仅点火。任务状态0～7依次IDLE、STARTING、RUNNING、STOPPING、COMPLETED、STOPPED、FAILED、UNKNOWN。

STATUS flags：bits0～2为logger（IDLE0/STARTING1/CAPTURING2/STOPPING3/FAULT4）；bits3～4为TF（UNKNOWN0/READY_CLOSED1/OPEN2/FAULT3）；bit5电池新鲜、bit6 ADC新鲜、bit7质量问题锁存。errors当前bit0质量错误，其余保留0。电池FFFF未知；没有向STATUS塞入系数、ADC统计或丢样明细。

RESULT result_state：低3位ACCEPTED0、STATE_REJECT1、EPOCH_REJECT2（实际错误会话直接丢弃）、OLD3、CONFLICT4、COMPLETED5、FAILED6；bits3～5为logger，bits6～7为TF。START完成须实际CAPTURING且TF OPEN；STOP完成须IDLE且文件已关闭。ACCEPTED只表示已接收并开始处理。

诊断DATA/1冻结23B：rate_millihz:u32@0、last_sequence:u32@4、durable_sequence:u32@8、dropped:u32@12、errors:u32@16、ADC_age_ms:u16@20、flags:u8@22。flags bits0～3为have_sample/have_durable/clock_verified/simulated。fragment10包含前16B，fragment11包含后7B。状态菜单的“质量”或“ADC”短按发QUERY/1，ADC行可显示该次查询快照；它不覆盖质量行。统计不是当前实时推力流。

## 5. 完整标定与本次置零

继续只支持一组当前传感器。`SENSOR.CFG`是操作者确认的传感器/ADC配置，`CAL0.BIN`、`CAL1.BIN`是原有交替提交的永久标定文件。启动自动读取，绑定不匹配拒绝套用；更换传感器必须更新SENSOR_ID并重新完整标定，软件不能自行识别物理更换。

COMMAND/3 action为BEGIN0、ZERO1、REFERENCE2、LOAD3、SAVE4、CANCEL5、REFERENCE_CLEAR6。只有REFERENCE后13B非零：unit g0/kg1/N2，milli_units为对应单位的千分之一；质量gravity显式给出，菜单采用9806650µm/s²，N的gravity为0。SAVE只有经TF同步、提交和读回验证后才确认持久化。

DATA/2的fragment30～33按顺序对应冻结64B视图的四段，可乱序重组：

| 偏移 | 类型 | 内容 |
|---:|---|---|
| 0/1/2/3 | 各u8 | state/error/flags/request_failed |
| 4/8 | 各u32 | samples/required |
| 12/16 | 各u32 | candidate_revision/active_revision |
| 20/24 | 各i32 | zero_raw/loaded_raw |
| 28/36 | 各i64 | slope_nN_per_count/reference_force_nN |
| 44 | u8 | unit |
| 45 | u64 | milli_units |
| 53 | u32 | gravity_um_s2 |
| 57 | 7B | 保留0 |

标定state 0～8为INACTIVE/ZERO_READY/ZERO_COLLECT/LOAD_READY/LOAD_COLLECT/REVIEW/COMMITTED/CANCELLED/FAILED；error 0～15为OK/CONFIG/BUSY/REFERENCE/ADC/SEQUENCE/CLOCK/TIMEOUT/UNSTABLE/SPAN/CONFLICT/STORAGE_UNBOUND/STORAGE_ERROR/ZERO_EXPIRED/COEFFICIENT/LINK。flags bits0～4为configured/storage_bound/idle/simulated/have_reference。完整四片匹配才更新视图，旧的“已保存”不会完成新的SAVE。120秒过期只取消未提交草稿。

独立“置零”COMMAND/4 action7仅采集本次稳定窗口，不重写永久标定；DATA/4 zero_state 0/1/2/3为IDLE/COLLECTING/READY/FAILED。窗口默认512样本、5秒超时、检查CRC/状态/饱和、连续序号、单调时间和原始码跨度，失败不可用于START。无载荷必须由操作者确认，稳定不等于无载荷。

自动置零开启时，START先取得新的稳定窗口，再创建TF记录；不会采集中持续追零。关闭时可使用手动建立的本次零点，否则使用永久标定。自动置零默认关闭；`SENSOR.CFG`可选 `AUTO_ZERO=0/1`作为启动默认值，菜单开关仅影响本次上电，避免为设置开关改写永久标定文件。

原始数据不变，永久斜率不变。STLOG001仍为512B头、32B原始记录；有效零点仍位于头36，斜率仍位于40。仅在本次置零有效时使用原保留区：`SZR1`@60、永久zero:i32@64、本次zero:i32@68、samples:u32@72、time_us:u64@76，84～507仍保留0；头CRC范围不变。旧导出器忽略保留区仍能按有效零点换算；新导出器额外报告session_zero元数据。CAL0/1二进制格式与NV分区不变。

## 6. ARM/FIRE/DISARM与有限动作

ARM表示限时准备，FIRE表示消耗该许可并请求一次MCU控制动作，DISARM表示撤销许可并要求MCU输出低。没有硬件反馈时，不能证明执行级已接入、点火头导通或点着，界面不显示“点火成功”。

请求body8：action:u8、selector:u8、argument:u32、profile:u16。QUERY0的selector为0快速/1详情，argument为0当前状态或原事务ID，profile0；ARM1的selector为1仅输出/2组合，argument0，profile为已查询配置；FIRE2携带相同模式、非零arm_id、相同profile；DISARM3后三字段均0。

回复body10的kind为命令结果0、查询主结果1、详情2。主结果布局：kind:u8/result:u8/state:u8/reason:u8/arm_id:u32/remaining_ms:u16。详情布局：kind:u8/known_mask:u16/pass_mask:u16/ignition_mV:u16/profile:u16/capability:u8。快速查询一包；详情查询冻结同一时刻的主结果和详情两包，完整匹配后采用。

result 0～11为REJECTED/ARM_GRANTED/FIRE_ACCEPTED/OUTPUT_ENDED/DISARMED/FAILED/NOT_SEEN/OLD_ID/CONFLICT/EXPIRED保留/RESULT_UNKNOWN/SNAPSHOT。state 0～6为DISABLED/SAFE/ARMED/FIRING/LOCKOUT/FAULT/BOOT_LOCKOUT保留。reason 0～6为NONE/UNBOUND/STATE/LEASE/PROFILE/LOCAL/OUTPUT，16为HISTORY。

详情known/pass bits0～12为物理许可、硬件禁止/急停、点火电源、外部反馈、自主截止、输出配置、项目校验、故障条件、记录关联、ADC、TF、配置互斥、射频时序依据；pass只能是known子集。capability bit0仅输出、bit1组合。未接入的外部量保持UNKNOWN，ignition_mV=FFFF。

**R8默认不绑定真实输出**：`ST_OUTPUT_HARDWARE_ENABLED=0`、`ST_OUTPUT_SAFETY_REVIEWED=0`、真实时长0；请求得到DISABLED/UNBOUND，profile为0。PA0初始化和故障路径保持低。1秒仅是软件模拟负载候选时长，不是火工品执行时长；不能把改宏当作已经通过安全审核。真实输出启用、时长及独立物理断能/故障截止需另行审核。

软件模拟绑定输出时，ARM许可10秒且不产生输出；第二次独立确认后FIRE才执行一次；动作结束为OUTPUT_ENDED/COMPLETED。主循环、返回菜单、重试、重连和重启都不产生第二次FIRE。组合模式的输出自然结束后，采集继续。组合STOP在采集端单一请求内撤销授权、截止输出、停止采集并完成TF收尾；不依赖另一包DISARM才变安全。输出菜单没有STOP按钮，安全截止接口和故障处理仍存在。

## 7. 四行GUI及完整触发流程

OLED128×64，每行16像素；ASCII宽6、所需中文子集宽12，UTF-8缓存每行64字节。默认中文，`ST_UI_CHINESE=0`编译英文。旋转选择，短按确认，800ms长按返回；超过四项滚动，光标保持可见，空白不参与选择。

主页依次“仅采集 / 采集与点火 / 仅点火 / 状态与设置”，默认第一项。进入所有子菜单默认反色“返回”。采集与组合均为“返回 / 开始·状态 / 停止 / 传感器设置”；仅输出为“返回 / 开始·状态 / 空 / 空”。传感器设置为“返回 / 置零 / 重新标定 / 自动置零”。状态菜单滚动显示返回、链路、电池、TF、质量、ADC、MCU VDDA、恢复连接、查询传感器。VDDA不称为遥控电池电压。

| 用户/运行事件 | 发出的协议与处理 |
|---|---|
| 上电尚未绑定 | HELLO/2 ↔ HELLO_ACK/2，空白NV保存对端UID |
| 已绑定启动、丢包恢复、手动恢复连接 | HELLO/0 → HELLO_ACK/0 → HELLO/1 → STATUS → QUERY/5 → DATA/5 |
| 正常运行 | 每2秒STATUS，遥控以RESULT/7F回收条；不会自动START |
| 采集/组合开始，确认页选择“确认开始” | COMMAND/1 → RESULT ACCEPTED → 可选稳定零点 → TF开始实际有效记录 → RESULT COMPLETED；停留当前页面显示采集中 |
| 运行中再点开始 | 采集模式不创建第二任务；组合模式只可打开独立ARM确认，不自动ARM/FIRE |
| 组合的独立准备确认 | COMMAND/20 ARM → 独立结果；获得许可才进入第二确认页，默认“返回” |
| 首次进入组合/仅输出页，尚未取得输出详情 | 按需QUERY/20 selector1取得配置；普通仅采集页面不周期查询输出 |
| 仅输出开始 | 独立ARM确认及独立FIRE确认；准备、控制中、已结束分别显示 |
| 明确确认控制动作 | COMMAND/20 FIRE仅发送一次；丢回复查询原ID，不自动重放 |
| 有限动作自然结束 | 本地截止，随后QUERY得到终态；仅输出显示已结束，组合继续采集 |
| 采集/组合停止，含START未确认 | 更高ID的COMMAND/2；取消待完成START、撤销授权/输出，TF sync/close后RESULT完成 |
| 普通返回/长按返回/状态过期/重连 | 只导航或恢复通信，不发送采集STOP；退出未执行的FIRE确认可发DISARM撤销准备 |
| 传感器设置进入/显示 | QUERY/4，之后最多每2秒刷新；置零确认发COMMAND/4 action7，开关发action8 |
| 重新标定进入/操作 | QUERY/2或COMMAND/3，返回四片DATA/2；保存持久化前不宣称已保存 |
| 状态“质量”/“ADC”短按 | QUERY/1，返回两片DATA/1 |
| 射频驱动故障 | 单独故障提示；本地ADC/TF记录继续，输出故障截止独立处理 |
| ADC/TF真实故障 | 保留原保护、排空/故障收尾；不虚报完成 |

## 8. 迁移、接线与验证边界

R7与R8无线不互通，必须同时升级两端；不加入自动降级。保留已配对的UID和原NV日志，升级不能全片擦除NV。F411保留0x08008000～0x08010000，应用从0x08010000；G071保留0x0801F000～0x08020000。稀疏HEX和现有链接边界不变。升级应在停机状态完成。

链路灯两端使用PB0：F411 LQFP64物理26脚、G071 LQFP32物理15脚，CubeMX已核对为空闲GPIO、默认低、高有效。各接独立限流电阻和LED到GND，阻值按实物LED电流选择。PA11/PA12预留、ADC DMA、SDIO/FatFs信号均未占用。PCB与接线仍需实物核对。

采集电池沿用PC0/ADC1_IN10/物理8脚的1:2分压，满电4.2V在ADC端约2.1V；禁止电池直接进ADC。PB6～9四颗绿色LED门限沿用3.90/3.70/3.50V、30mV回差，低于3.30V末灯1Hz闪，未知/过期熄灭。这是电压档位，不是精密SOC或电池保护。

验证入口：`run_host_tests.py`（默认中文，`--english`英文）、`check_link_runtime.py`、`check_reference.py`、`check_ignition_f4.py`、`validate_candidates.py`、`build_reference.py --artifacts`。模型覆盖去重、顺序、恢复、TF收尾、永久标定恢复、会话零点、中英文四行与有限模拟输出；不代表无线距离、硬件、电气或点火安全验收。完整交付清单见 `R8_DELIVERY.md`。
