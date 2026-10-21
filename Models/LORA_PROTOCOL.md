# SwiftThrust 完整 LoRa 协议与操作流程 R7

2026-10-21。两端 ReferenceApp 已接通自动连接、采集/标定、点火菜单和采集板 PA0 的5秒逻辑脉冲。新增电池检测使用 PC0。尚未烧录或上板测试。

ARM 是限时准备，FIRE 是消耗许可并启动一次输出，DISARM 是撤销准备并强制 MCU 信号低。固件只报告 MCU 控制状态，不能判断执行级是否接上、点火头是否导通或是否点着。外部反馈始终为 UNKNOWN。

## 射频与通用格式

保留2473MHz、+12dBm、SF10、BW812500Hz、CR4/5、sync0x12、16符号前导、显式物理头、PHY CRC、normal IQ、500ms TX超时。双端仅额外开启高灵敏度LNA、保留AGC。同频之外，上述调制/分帧参数也必须匹配。

每个物理包放一个应用帧，最大35B。没有应用版本、魔数或长度字段；首字节只有TYPE。多字节整数均小端，UID和标签是原始字节。末尾CRC16/CCITT-FALSE：poly0x1021、init0xFFFF、非反射、xorout0，覆盖此前全部字节，低字节先发；123456789校验为0x29B1。先检查精确长度和CRC，再检查字段、会话及认证。

普通时序保持：心跳10000ms、最大空中时长预算150ms、ACK窗口500ms、总操作期限120000ms、最多4次，重试抖动0～100ms。链路新鲜阈值31150ms。ADC高频原始流写TF，不通过LoRa逐样本传输。

## 全部包型与精确布局

C=遥控端，A=采集端。下表数字为字节数，字段按线上顺序列出。E=非零epoch:u32，ID=非零u16，CRC=末尾2B。

| TYPE | 方向 | 名称/总长 | 字段顺序 |
|---|---|---|---|
| 01 | A→C | STATUS 12B | TYPE1,E4,seq2,flags1,mV2,CRC2 |
| 02 | C→A | START 9B | TYPE1,E4,ID2,CRC2 |
| 03 | C→A | STOP 9B | TYPE1,E4,ID2,CRC2 |
| 04 | A→C | ACK 10B | TYPE1,E4,ID2,result_state1,CRC2 |
| 05 | C→A | QUERY 10B | TYPE1,E4,ID2,section1,CRC2 |
| 06 | A→C | REPLY 11～27B | TYPE1,E4,ID2,section1,fragment1,data0～16,CRC2 |
| 07 | C→A | HELLO 35B | TYPE1,target_acq_UID12,controller_UID12,counter8,CRC2 |
| 08 | A→C | HELLO_REPLY 27B | TYPE1,acq_UID12,counter8,E4,CRC2 |
| 09 | C→A | COMMIT 15B | TYPE1,E4,counter8,CRC2 |
| 0A | C→A | CAL_COMMAND 23B | TYPE1,E4,ID2,action1,unit1,milli_units8,gravity4,CRC2 |
| 0B | C→A | CAL_QUERY 9B | TYPE1,E4,ID2,CRC2 |
| 0C | A→C | CAL_REPLY 每片26B | TYPE1,E4,ID2,fragment1,data16,CRC2 |
| 0D | C→A | IGN_REQUEST 33B | TYPE1,E4,ID2,action1,selector1,argument4,profile2,CMAC16,CRC2 |
| 0E | A→C | IGN_REPLY 35B | TYPE1,E4,ID2,payload10,CMAC16,CRC2 |
| 0F | C→A | DISCOVER 33B | TYPE1,network2,sender_UID12,CMAC16,CRC2 |
| 10 | A→C | ADVERTISE 33B | TYPE1,network2,sender_UID12,CMAC16,CRC2 |

旧01～0C保持原字节布局；R7采集端STATUS的mV明确改为单节电池电压，遥控SELF仍是自身MCU VDDA。电池无效为FFFF，过期保留最近值但清新鲜位。0mV在线上仍是合法数值，不表示未知。

## 自动发现和会话

两端内置network=0x5354和相同项目校验密钥，不要求用户输入密钥或抄UID。固定校验密钥在公开源码中，作用主要是隔离误包，不能抵御读取源码后仿造同配置设备的攻击。

首次空白遥控器每2s发0F。空白采集器校验后，在供电正常、采集/标定空闲时持久保存该遥控UID，回复10；已有相同对端也能重复回复，不重复落盘。遥控器验证回复后持久保存采集UID。首次仅为一对同配置设备上电。记录损坏、丢失或已有其他UID时不会自动覆盖、清计数或重新绑定。

发现帧偏移：TYPE@0、network@1、sender_UID@3、tag@15、CRC@31。CMAC输入：ASCII STDISC1（7B无NUL）＋前15B帧前缀＋目标UID12B。0F目标是12个0；10目标是发起发现的遥控UID。

保存对端以后，遥控端自动持久递增counter并发07；采集端核对完整UID和更大的counter，持久提交新epoch后才回08；遥控发09，采集激活后回01。收到有效递增STATUS才进入LINK_UP。重复同一次HELLO不重复分配。自动采集器使用网络段1：epoch为01000001～01FFFFFF，认证上下文另包含双方UID。

已记住对端时上电自动握手；链路过期后，遥控在本地空闲时重新握手，重试间隔至少2s。STATUS页CONNECT保留为手动重试。重启/重连不恢复START、ARM或FIRE；新的HELLO撤销旧授权并关闭MCU信号。NV损坏、耗尽或写入失败时拒绝建链，不能回退RAM防重放。

07～09及原采集/标定包无密码认证，存在伪造和拒绝服务的边界，不能单独授权点火。首次自动发现及固定项目密钥依赖用户确认的单对设备使用条件。

## 状态、采集和诊断

STATUS flags：bits0～2为IDLE0、STARTING1、CAPTURING2、STOPPING3、FAULT4；bits3～4为TF UNKNOWN0、READY_CLOSED1、OPEN2、FAULT3；bit5电池测量新鲜、bit6 ADC新鲜、bit7质量问题锁存。

ACK result_state：低3位为ACCEPTED0、STATE_REJECT1、EPOCH_REJECT2（错误epoch实际丢弃）、OLD3、CONFLICT4、COMPLETED5、FAILED6；bits3～5为采集状态，bits6～7为TF状态。

START只表示开始日志准备；进入真正记录后才回COMPLETED。STOP先禁新样本、排空、写尾、sync/close后才完成。STATUS或ACCEPTED不能替代终态ACK。START待确认时可以排队更高ID的STOP；晚到旧START失效。

命令ID严格递增1～65535，不回绕；相同ID同命令取缓存，不重执行，改变内容冲突，旧ID拒绝；耗尽需新会话。普通服务保留最近事务；诊断查询使用独立ID、不清除命令高水位。未知TYPE/字段/保留位、错误长度/CRC/epoch均拒绝。

section=1诊断冻结23B：rate_millihz:u32@0、last_sequence:u32@4、durable_sequence:u32@8、dropped:u32@12、source_errors:u32@16、ADC_age_ms:u16@20、flags:u8@22。flags bits0～3依次have_sample、have_durable、clock_verified、simulated，其余0；ADC年龄FFFF为未知或超范围。片0 fragment=10/data16/总27B，片1 fragment=11/data7/总18B。同epoch/ID重复查询重发原快照；全部收齐且无冲突才更新视图。

## 标定

0A action=BEGIN0、ZERO1、REFERENCE2、LOAD3、SAVE4、CANCEL5、REFERENCE_CLEAR6。只有REFERENCE允许后13B非零，其余动作后13B全0。unit=g0、kg1、N2，milli_units是单位的千分之一。质量gravity显式指定，菜单用9806650μm/s²；N的gravity为0。

0C冻结64B视图按fragment=30～33分四片：

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

state依次INACTIVE0、ZERO_READY1、ZERO_COLLECT2、LOAD_READY3、LOAD_COLLECT4、REVIEW5、COMMITTED6、CANCELLED7、FAILED8。error依次OK0、CONFIG1、BUSY2、REFERENCE3、ADC4、SEQUENCE5、CLOCK6、TIMEOUT7、UNSTABLE8、SPAN9、CONFLICT10、STORAGE_UNBOUND11、STORAGE_ERROR12、ZERO_EXPIRED13、COEFFICIENT14、LINK15。flags bits0～4依次配置有效、存储已绑定、idle、simulated、有reference。

四片按epoch/ID完整重组。重复SAVE不重复写入，丢回复必须查询实际活动修订；120s租期只取消未提交草稿。采集或点火准备/运行期间拒绝改动标定；进入采集START还检查并应用活动profile。

## 点火请求与回复

0D精确偏移：TYPE@0、epoch@1、ID@5、action@7、selector@8、argument@9、profile@13、tag@15、CRC@31。

| action | selector | argument | profile |
|---|---|---|---|
| QUERY0 | 0快速/1详情 | 0当前状态，或目标命令ID≤65535 | 0 |
| ARM1 | 1仅点火/2组合 | 0 | 1，固定5秒MCU信号 |
| FIRE2 | 与授权模式相同 | 非零arm_id | 与授权相同 |
| DISARM3 | 0 | 0 | 0 |

0E精确偏移：TYPE@0、epoch@1、ID@5、payload@7、tag@17、CRC@33。payload首字节kind=命令结果0、查询主结果1、详情2。主结果为kind1/result1/state1/reason1/arm_id4/remaining_ms2。ARMED的remaining表示授权剩余时间，FIRING表示脉冲流程剩余时间，否则0。

详情payload为kind1/known_mask2/pass_mask2/ignition_mV2/profile2/capability1。known/pass bits0～12依次物理许可、硬件禁止/急停、点火电源、输出反馈、自主截止、输出配置、密钥配置、故障条件、记录关联、ADC、TF、配置互斥、射频时序依据；保留bits13～15为0，pass只能是known子集。capability bit0仅点火、bit1组合，其余0。

当前ReferenceApp known=pass=0070，只表示本地定时截止、MCU信号配置和项目校验配置；profile=1、capability=3。点火电源FFFF、外部许可/急停/反馈均未知。自主截止是MCU定时器，输出配置不是执行级存在证明。未绑定输出的组件仍回复DISABLED/UNBOUND、profile=0。

快速查询一包主结果；详情查询主结果加详情两包，来自同一冻结快照。乱序和相同重复可接收，缺片或内容冲突不能更新完整视图。命令与查询各用独立单调ID；ARM/FIRE/DISARM各保留一个缓存，查询不挤掉命令结果。新的DISARM提高命令高水位，尚未执行的更低ID请求失效。

result：REJECTED0、ARM_GRANTED1、FIRE_ACCEPTED2、OUTPUT_ENDED3、DISARMED4、FAILED5、NOT_SEEN6、OLD_ID7、CONFLICT8、EXPIRED9（保留）、RESULT_UNKNOWN10、SNAPSHOT11。OUTPUT_ENDED与DISARMED只报告MCU操作，不包含执行级反馈或物理点着的判断。

state：DISABLED0、SAFE1、ARMED2、FIRING3、LOCKOUT4、FAULT5、BOOT_LOCKOUT6（保留）。reason：NONE0、UNBOUND1、STATE2、LEASE3、PROFILE4、LOCAL5、OUTPUT6、HISTORY_UNAVAILABLE16。

启动强制低并进入SAFE。ARM合格后进入ARMED，输出仍低，许可10s。arm_id取本次ARM命令ID，由认证中的持久epoch及双方UID约束上下文。FIRE必须匹配模式/profile/arm_id且未超时，执行前消耗授权；PA0启动一次5s脉冲。重复FIRE取缓存，不重启、不延长。正常结束进入LOCKOUT并把FIRE缓存更新为OUTPUT_ENDED；再次开始前须STOP/DISARM回SAFE。

仅点火要求采集IDLE、MCU VDDA正常、无线无故障、标定未锁；组合要求CAPTURING、ADC/速率/TF正常且无源/日志错误。授权超时或本地条件失效会撤销准备；运行中本地条件失效会提前截止。未知外部状态不冒充检查通过。

CMAC输入：ASCII STIGN2（6B无NUL）＋direction:u8（请求0/回复1）＋acq_UID12＋ctrl_UID12＋帧前缀（请求15B/回复17B）。完整16B AES-CMAC，不加密；CRC另覆盖标签。错误认证静默丢弃，不更新ID或缓存。

QUERY/ARM/DISARM同字节同ID有界重试，从实际TX_DONE开始计时；详细查询用两倍ACK窗口。FIRE只向无线队列提交一次，丢回复或TX超时显示UNKNOWN，不自动补发。授权截止按第一次队列接受时间加回复剩余时间保守计算；过期且未发射的排队FIRE会在radio tick前删除。

DISARM优先于尚未发射的请求，可替换查询/ARM/FIRE；已经在空中的包不能撤回。远程STOP可能丢包，本地5秒截止独立于无线和主循环。原FIRE的查询结果与当前state应一起理解：曾接受不表示现在仍输出。

## 什么时候发送与接收

| 场景 | C发送 | A处理与回复 |
|---|---|---|
| 首次空白上电 | 每2s DISCOVER | 保存对端，ADVERTISE；C保存A |
| 已配对上电、链路过期、手动CONNECT | HELLO，随后COMMIT | 持久新epoch、HELLO_REPLY、STATUS；撤销旧输出 |
| 正常空闲 | 无高频流 | 约10s STATUS；状态变化可提前发送 |
| 仅采集START确认 | START | 接受ACK，真正记录后完成ACK |
| 仅采集STOP | STOP | 排空/sync/close后完成ACK |
| 进入点火/组合菜单 | 详细IGN QUERY | 主状态＋详情 |
| 仅点火START确认 | ARM | 合格回ARM_GRANTED，信号保持低 |
| 组合START确认 | 先START采集，完成后自动ARM | CAPTURING后才允许准备许可 |
| 已准备 | 不自动发FIRE；独立确认页默认BACK | 最多等待10s，超时撤销 |
| 旋转到CONFIRM FIRE并短按 | FIRE一次 | 启动PA0单次脉冲，FIRE_ACCEPTED |
| 点火菜单空闲、没有准备许可 | 最多每1.5s QUERY | 当前状态，可查询原FIRE结果 |
| 脉冲自然到期 | 不依赖报文 | 定时器自行低，后台更新完成缓存 |
| STOP或BACK | DISARM，组合同时创建/排队采集STOP | 关闭MCU信号、回复；另完成采集收尾ACK |
| 标定页面进入/操作 | CAL_QUERY/CAL_COMMAND | 四片CAL_REPLY |
| 显式诊断API | 05 QUERY | 06两片；STATUS菜单不自动发该诊断 |
| FIRE回复丢失 | 查询原FIRE ID | 返回历史与当前MCU状态；未知保持UNKNOWN |

组合模式脉冲结束后继续记录推力，直到STOP/BACK。仅点火页STOP/BACK不借用采集STOP。普通STATUS不能完成点火事务。

## 引脚、接线和验证

点火信号：F411 PA0，LQFP64物理14脚，AF1 TIM2_CH1。接外部执行级的3.3V兼容、高有效逻辑输入，执行级信号地接采集板GND，输入端约10kΩ下拉。MCU引脚不直接接点火头、负载或继电器线圈；隔离输入按执行级规范连接。PA11/PA12预留不变。

CubeMX候选16MHz用PSC15，ReferenceApp实际定时器84MHz由时钟树算PSC83；均为1MHz。ARR=5000000、CCR1=1、PWM2、OPM：启动后约1μs高，持续名义5s，更新事件回CNT0/低并清CEN。时钟正常运行时，主循环/中断不服务也能截止。HSI决定实际脉宽误差，仍需测量。

电池采样：PC0 / LQFP64物理8脚 / ADC1_IN10，10kΩ从开关后的BAT+到PC0，10kΩ及100nF从PC0到GND。满电4.2V在ADC端约2.1V。VREFINT补偿VDDA；低速交替测量两路，不占用外部ADS131M02推力ADC。电池不能直接接PC0。

PB6/PB7/PB8/PB9连接四颗绿色LED，各串独立限流电阻，默认高有效。电量粗略分档：≥3.90V四灯，3.70～3.90V三灯，3.50～3.70V两灯，低于3.50V一灯；最后一档按3.50～约3.10V考虑，低于3.30V最后一灯1Hz闪烁。30mV回差与滤波抑制跳灯，无效或超过1s未更新时全灭。不是精密SOC或电池保护器。

无线对照了本地SilverStar飞控快照的Flight/Generated/Inc/air_link_config.h，并只读查看[SilverStar](https://github.com/aleistercrowleybeast666/SilverStar)无线契约；不混用AIR业务帧。定时器参考[ST RM0383](https://www.st.com/resource/en/reference_manual/dm00119316.pdf)。电池门限参考[Panasonic NCR18650B放电曲线](https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/196/Panasonic-NCR18650B-datasheet.pdf)作工程近似，负载/温度/电芯型号会改变指示。

先用示波器/逻辑分析仪或指示负载核对信号及分压。没有物理保险/急停输入、导通检测和执行级反馈，不能把MCU信号状态当作这些功能已实现。离线测试与ARM编译日志均在Models，实际菜单帧缓冲见[菜单预览](ignition_ui_preview.png)。本轮没有访问串口/探针或烧录。
