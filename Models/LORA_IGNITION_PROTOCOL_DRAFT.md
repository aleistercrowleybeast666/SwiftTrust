# SwiftThrust 完整 LoRa 协议与报文触发流程

> 以下为R6协议阶段的历史记录，已经被后续实现替代。**当前完整协议与行为以 [R8文档](LORA_PROTOCOL_R8.md) 为准，真实输出默认关闭。**

2026-10-21，文档修订R6。原文件名保留，内容已由四种点火包草案收敛成实际实现的两种包。两端已接入认证请求、响应、缓存、重试和查询；**本轮不接入点火GPIO、执行驱动、物理许可或输出反馈，采集端固定为DISABLED并拒绝ARM/FIRE。**

ARM是预备授权，FIRE是提交一次执行请求，DISARM是撤销授权并请求关闭输出。协议收包成功不能证明执行电路存在、已经关闭或已经点火；当前固件没有自动检测这些事实的能力，也不根据未接GPIO的读值猜测硬件状态。

射频保持2473MHz、+12dBm、SF10/BW812500/CR4/5、前导16 symbols、PHY CRC、原有超时与10s心跳；仅在两端初始化中显式启用高灵敏度LNA，保留AGC。ADC、TF与STLOG001格式不变。

## 1 现有工程依据与兼容范围

| 项目 | 当前实际依据 | 本方案处理 |
|---|---|---|
| 无线编码 | `Acquisition/include/st_link2.h`、`src/st_link2.c` | 沿用 TYPE、小端、CRC16、epoch、事务去重 |
| 最大应用长度 | `ST2_MAX=35`；`st_radio.c` 在收包时也检查类型与长度 | 保持35B，解析器、报文结构与无线入口已接入0x0D/0x0E |
| 当前射频参考 | `st_reference.c`：2473MHz、+12dBm、SF10/BW812500/CR4/5 | 不把参考参数或历史距离当作本次板测结果 |
| 当前时序参考 | 心跳10000ms、最大空中时间150ms、ACK等待500ms、操作总时限120000ms、最多4次 | 继续服务采集；点火使用独立授权期限及重试规则 |
| 遥控界面 | SSD1306 128×64；每页8行，每行最多21个 ASCII 字符 | 保留状态、仅点火、仅采集、点火并采集四入口 |
| 输入 | TIM3编码器、PA2去抖短按；现有主循环未接入长按或实体BACK事件 | 点火长按、松手检测与返回行为都属于后续实现 |
| 供电 | 当前实测字段是 MCU VDDA | 点火电源必须独立测量，不能拿 VDDA 代替 |
| 安全输出 | PA11/PA12仅未配置预留；没有执行级接口 | 不在本草案分配管脚或确定执行电路参数 |

R6不增加空中版本字段或版本位，原0x01～0x0C布局不变。点火扩展只有0x0D请求、0x0E回复；旧R5四包草案未部署，现由本契约替代。旧固件拒绝新类型；新包必须验证CMAC，不允许回退成普通START/STOP。

## 2 通用格式与校验

`u8/u16/u32/u64`为无符号整数，`i32/i64`为补码有符号整数。偏移和长度均以字节计，**所有多字节数值均为小端**；UID及认证标签是原始字节串，不做整数端序转换。

每个 LoRa 物理包承载恰好一个应用帧，以显式物理头的载荷长度分帧。应用层没有额外包头魔数、长度字段或版本字段。先核对类型及精确长度，再读取字段。未知类型、非法值、保留位非零、错误长度、错误 CRC、无效会话均拒绝。

末尾 CRC16 为 CCITT-FALSE：poly=0x1021，init=0xFFFF，非反射，xorout=0；覆盖它前面的所有字节，包括新增帧的认证标签。CRC低字节先发；ASCII `123456789`应得到0x29B1。LoRa物理CRC也保持开启。CRC只检查误码，不能代替身份认证。

会话短包通常以 `TYPE:u8 + link_epoch:u32 + ID:u16` 开头；握手三包按各自布局编码。ID和epoch必须非零，除非某字段明确允许0。

## 3 全部包型

方向C表示遥控端，A表示采集端。

| TYPE | 名称 | 方向 | 应用总长 | 内容 |
|---|---|---|---:|---|
| 0x01 | STATUS | A→C | 12B | 采集状态与MCU电压 |
| 0x02 | START | C→A | 9B | 开始采集 |
| 0x03 | STOP | C→A | 9B | 停止采集 |
| 0x04 | ACK | A→C | 10B | 采集事务结果 |
| 0x05 | QUERY | C→A | 10B | 采集诊断请求 |
| 0x06 | REPLY | A→C | 11～27B | 采集诊断分片 |
| 0x07 | HELLO | C→A | 35B | UID和持久计数握手 |
| 0x08 | HELLO_REPLY | A→C | 27B | 持久分配新epoch |
| 0x09 | COMMIT | C→A | 15B | 确认会话 |
| 0x0A | CAL_COMMAND | C→A | 23B | 标定动作 |
| 0x0B | CAL_QUERY | C→A | 9B | 标定快照请求 |
| 0x0C | CAL_REPLY | A→C | 每片26B，共4片 | 标定冻结快照 |
| 0x0D | IGN_REQUEST | C→A | 33B | 认证查询、ARM、FIRE、DISARM |
| 0x0E | IGN_REPLY | A→C | 每包35B | 主结果或按需详情，最多两包 |

## 4 握手与配对

| 包 | 字段顺序及偏移 |
|---|---|
| HELLO 35B | TYPE@0；目标采集UID[12]@1；遥控UID[12]@13；遥控持久计数u64@25；CRC@33 |
| HELLO_REPLY 27B | TYPE@0；采集UID[12]@1；回显计数u64@13；新epoch u32@21；CRC@25 |
| COMMIT 15B | TYPE@0；epoch u32@1；计数u64@5；CRC@13 |

遥控端先持久增加计数，再发送HELLO。采集端核对完整配对UID，仅接受大于持久历史的新计数；**持久提交计数和从不复用的epoch以后**才回复。相同提议的重复HELLO沿用同一结果，不反复分配。COMMIT后发送STATUS，遥控端收到有效递增STATUS才认为普通采集链路已建立。

服务端重启无活跃会话，旧短包失效。计数、epoch耗尽、NV写入失败或历史损坏均拒绝建链，不用RAM或随机数替代持久防重放。当前参考应用已接片内Flash日志后端，真实掉电及磨损行为仍待板测；旧文档中的“无物理NV后端”是早期状态。

R6参考应用仍只执行普通采集与协议处理，不存在点火授权或输出。显式CONNECT取消本地点火协议在途请求，重新握手后使用新epoch，不能恢复旧请求；已经在空中的包无法撤回。设置/清除临时密钥同样先退休会话，随后必须显式CONNECT。

现有握手、普通采集与标定包没有密码认证，仍可能受到伪造或拒绝服务影响。点火不能依据这些包单独判断身份或就绪。

## 5 状态与采集启停

### 5.1 字段布局

| 包 | 字段顺序及偏移 |
|---|---|
| STATUS 12B | TYPE@0；epoch u32@1；status_sequence u16@5；flags u8@7；mV u16@8；CRC@10 |
| START / STOP 9B | TYPE@0；epoch u32@1；transaction_id u16@5；CRC@7 |
| ACK 10B | TYPE@0；epoch u32@1；transaction_id u16@5；result_state u8@7；CRC@8 |

STATUS flags：bits0～2为采集状态，bits3～4为TF状态，bit5电压新鲜，bit6 ADC新鲜，bit7质量问题锁存摘要。

| 采集状态值 | 含义 | TF值 | 含义 |
|---:|---|---:|---|
| 0 | IDLE | 0 | UNKNOWN |
| 1 | STARTING | 1 | READY_CLOSED，真实就绪且关闭 |
| 2 | CAPTURING | 2 | OPEN |
| 3 | STOPPING | 3 | FAULT |
| 4 | FAULT | — | — |

mV=0～65534表示最近有效测量，0xFFFF表示未知/无效；过期可保留最近值但清新鲜位。真实0mV有效。当前参考应用该字段代表采集MCU VDDA，不能显示为点火电池或精确电池SOC。

ACK的bits0～2为结果，bits3～5为采集状态，bits6～7为TF摘要。结果：0 ACCEPTED、1 STATE_REJECTED、2 EPOCH_REJECTED（保留，实际错误epoch静默丢弃）、3 OLD_ID、4 CONFLICT、5 COMPLETED、6 FAILED；7保留。

### 5.2 事务语义

START/STOP/CAL_COMMAND/CAL_QUERY共用1～65535的严格递增事务流，不回绕；耗尽重新建链。一般只允许一笔未完成事务。相同ID和相同完整请求返回缓存结果，不重复执行；同ID改变内容为CONFLICT，旧ID拒绝。

ACCEPTED只表示接受。START在日志头写入/sync并进入CAPTURING后报告同ID的COMPLETED；STOP在排空、结束记录、sync、close并回IDLE后报告COMPLETED。失败报告FAILED。终态ACK丢失时，重发原采集命令可读取缓存终态；STATUS不能独立完成某笔事务。

START待确认时可排队更高ID的STOP，取消STARTING或阻止晚到START。客户端必须先处理已发射请求的TX完成/不确定性，不能假装撤回空中包。新连接后不自动重发新ID的START。

R6当前实现中，普通START/STOP仍只控制采集。点火页及联动页的STOP发送认证DISARM，不借用普通STOP，也不自动停止独立开始的记录。由于没有执行级或反馈，DISARM不能回复“已确认物理关闭”；当前也没有可被普通START/STOP绕过的点火授权。

## 6 按需采集诊断

QUERY 10B：`TYPE@0 + epoch:u32@1 + request_id:u16@5 + section:u8@7 + CRC@8`。

REPLY 11～27B：`TYPE@0 + epoch:u32@1 + request_id:u16@5 + section:u8@7 + fragment:u8@8 + data[0..16]@9 + CRC`。

fragment高4位为总片数减1，低4位为片索引。当前section=1为23B冻结快照：

| 快照偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 4 | 实际rate_millihz，时钟依据未验证则0 |
| 4 | 4 | 最近样本序号 |
| 8 | 4 | 最近durable序号 |
| 12 | 4 | 环形队列累计丢样 |
| 16 | 4 | ADC/源错误累计数 |
| 20 | 2 | ADC年龄ms，0xFFFF未知或超范围 |
| 22 | 1 | bit0有样本；bit1有durable；bit2时钟依据已验证；bit3合成数据；其余0 |

片0：fragment=0x10、data16B、整包27B；片1：0x11、data7B、整包18B。

诊断request_id独立递增，不改变采集事务高水位。同epoch和ID重读同一冻结快照；新ID采新快照，旧ID拒绝。允许乱序和相同重复，拒绝重复片内容冲突；全部收齐才更新视图。该快照不作为点火认证状态。

## 7 标定协议

CAL_COMMAND 23B：`TYPE@0 + epoch:u32@1 + id:u16@5 + action:u8@7 + unit:u8@8 + milli_units:u64@9 + gravity_um_s2:u32@17 + CRC@21`。

CAL_QUERY 9B与START的字段布局相同。CAL_REPLY每片26B：`TYPE@0 + epoch:u32@1 + id:u16@5 + fragment:u8@7 + data[16]@8 + CRC@24`；四片的fragment为0x30～0x33。

动作值0 BEGIN、1 ZERO、2 REFERENCE、3 LOAD、4 SAVE、5 CANCEL、6 REFERENCE_CLEAR。仅REFERENCE允许后13B非零，其他动作必须全0。unit=0 g、1 kg、2 N；milli_units是所选单位的千分之一，1000分别表示1g、1kg或1N。质量采用显式gravity，菜单约定9806650µm/s²；N输入的gravity=0。

64B冻结视图完整布局：

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 1 | state |
| 1 | 1 | error |
| 2 | 1 | flags：配置有效、存储已绑定、idle、simulated、有reference依次为bit0～4 |
| 3 | 1 | 本次请求失败0/1 |
| 4 / 8 | 各4 | samples / required |
| 12 / 16 | 各4 | candidate_revision / active_revision |
| 20 / 24 | 各4 | zero_raw / loaded_raw，i32 |
| 28 / 36 | 各8 | slope_nN_per_count / reference_force_nN，i64 |
| 44 | 1 | unit |
| 45 | 8 | milli_units |
| 53 | 4 | gravity_um_s2 |
| 57 | 7 | 保留0 |

state=0～8依次INACTIVE、ZERO_READY、ZERO_COLLECT、LOAD_READY、LOAD_COLLECT、REVIEW、COMMITTED、CANCELLED、FAILED。

error=0～15依次OK、CONFIG、BUSY、REFERENCE、ADC、SEQUENCE、CLOCK、TIMEOUT、UNSTABLE、SPAN、CONFLICT、STORAGE_UNBOUND、STORAGE_ERROR、ZERO_EXPIRED、COEFFICIENT、LINK。

四片只在同epoch/id下完整重组；重复SAVE不重复落盘。丢回复显示结果未知，后续查询活动修订核对。超出操作租期只取消未提交草稿；已保存结果保留。采集中不允许标定、SAVE或替换参数；点火授权状态机本轮不接入。

## 8 精简后的认证点火协议

### 8.1 33B请求

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 1 | TYPE=0x0D |
| 1 | 4 | 当前link_epoch |
| 5 | 2 | ID，命令与查询各有独立单调流 |
| 7 | 1 | action：0 QUERY、1 ARM、2 FIRE、3 DISARM |
| 8 | 1 | selector：查询为0快速/1详情；ARM/FIRE为1仅点火/2联动；DISARM为0 |
| 9 | 4 | argument：查询为目标命令ID；ARM为0；FIRE为非零arm_id；DISARM为0 |
| 13 | 2 | ignition_profile_id：ARM/FIRE非零；QUERY/DISARM为0 |
| 15 | 16 | AES-CMAC标签 |
| 31 | 2 | CRC16 |

查询argument=0获取当前状态；非0查询本epoch内指定命令的结果，数值必须≤65535。ARM回显当前配置号，不能靠该字段改变硬件配置。FIRE须回显已确认授权的模式、arm_id和配置号；这轮采集端不会发放任何授权。

命令ID和查询ID分别为1～65535，不回绕。查询不提升命令高水位，不挤掉命令结果；同ID同请求读原缓存，同ID异请求冲突。新DISARM提升命令高水位，尚未处理的更低ID命令成为旧请求。查询不能清除故障、制造授权或证明输出关闭。

### 8.2 35B回复

共同布局：`TYPE=0x0E@0 + epoch:u32@1 + 回显ID:u16@5 + payload[10]@7 + CMAC[16]@17 + CRC16@33`。

payload首字节kind区分内容：0命令结果，1查询主结果，2查询详情。因此查询ID与命令ID相同也不会混淆；接收端必须同时匹配epoch、ID和kind。

kind=0/1的主结果：

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 7 | 1 | kind=0或1 |
| 8 | 1 | result |
| 9 | 1 | 当前state |
| 10 | 1 | reason |
| 11 | 4 | 当前/最近arm_id，无则0 |
| 15 | 2 | 授权剩余ms，无授权0，未知0xFFFF |

kind=2的9B详情：

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 7 | 1 | kind=2 |
| 8 | 2 | known_mask，各检查条件是否有真实依据 |
| 10 | 2 | pass_mask，只能是known_mask子集 |
| 12 | 2 | 独立点火电源mV，0xFFFF未知，不能用MCU VDDA代替 |
| 14 | 2 | 当前ignition_profile_id，0未绑定 |
| 16 | 1 | capability：bit0仅点火、bit1联动，其余0 |

known/pass的bits0～12依次为：物理许可周期、硬件禁止链/急停、点火电源、输出反馈、独立截止、硬件配置、密钥配置、故障条件、当前记录关联、ADC条件、TF条件、配置/标定互斥、射频时序核验。bits13～15保留0；known=0表示未知，不能当成通过。

**当前详情只有bit6密钥配置为已知且通过，其余条件保持未知；点火电源为0xFFFF、profile=0、capability=0。** 没有执行级接口，不存在通过“打开一个已绑定标志”就能启用GPIO的后门。

快速查询返回一包主结果。详细查询返回主结果加一包详情，来自同一冻结快照；重试同ID返回原内容。允许乱序/相同重复；有内容冲突或缺包就不更新已确认的完整视图。

### 8.3 结果与状态

| result | 含义 | 当前采集端是否产生 |
|---:|---|---|
| 0 REJECTED | 请求被拒绝 | ARM/FIRE固定返回 |
| 1 ARM_GRANTED | 已授予一次性软件许可，不代表输出 | 保留，不产生 |
| 2 FIRE_ACCEPTED | 已接受一次执行请求，不代表物理点着 | 保留，不产生 |
| 3 OUTPUT_ENDED | 输出流程结束且反馈正常 | 保留，不产生 |
| 4 DISARMED | 授权撤销且关闭反馈正常 | 保留，不产生 |
| 5 FAILED | 无法确认执行/关闭结果 | 当前DISARM固定返回 |
| 6 NOT_SEEN | 快照时目标ID高于已处理高水位 | 查询可能产生 |
| 7 OLD_ID | 命令ID过期 | 可能产生 |
| 8 CONFLICT | 同ID请求内容冲突 | 可能产生 |
| 9 EXPIRED | 授权已过期 | 保留，不产生 |
| 10 RESULT_UNKNOWN | 目标历史已不可用 | 查询可能产生 |
| 11 SNAPSHOT | 当前状态查询完成 | QUERY target=0返回 |

state=0 DISABLED、1 SAFE、2 ARMED、3 FIRING、4 LOCKOUT、5 FAULT、6 BOOT_LOCKOUT。**当前采集端只报告DISABLED、arm_id=0、剩余ms=0。** 其他状态供协议解析和后续独立硬件设计使用，不代表其硬件状态机已实现。

当前reason=1 UNBOUND，或16 HISTORY_UNAVAILABLE。其余0～15中的代码保留：0 NONE、2 LOCAL_INHIBIT、3 ESTOP、4 POWER、5 OUTPUT_FEEDBACK、6 CUTOFF_UNVERIFIED、7 PROFILE、8 LEASE_EXPIRED、9 CAPTURE_NOT_READY、10 ADC、11 STORAGE、12 BUSY、13 LOCAL_REARM_REQUIRED、14 RF_TIMING、15 INTERNAL。

NOT_SEEN仅说明快照时还未处理，不能证明一个仍在空中的请求以后不会到达；不能据此补发新ID的FIRE。命令缓存分别保留最近ARM、FIRE、DISARM，查询不会覆盖；已淘汰的历史返回RESULT_UNKNOWN，不能伪装成未执行。

### 8.4 认证和测试密钥

`CMAC = AES-CMAC(K, ASCII("STIGN2") || direction:u8 || acquisition_UID[12] || controller_UID[12] || frame_prefix)`。

ASCII无末尾NUL，direction为请求0/回复1，UID顺序固定；frame_prefix覆盖TYPE到TAG之前所有字节。TAG为完整16B原始输出，CRC再覆盖TAG。认证使用[RFC4493](https://www.rfc-editor.org/rfc/rfc4493.html)算法和固定提交的[tiny-AES-c](https://github.com/kokke/tiny-AES-c)AES分组实现，第三方原始源码、许可和哈希在`Acquisition/vendor/tiny_aes`保留。

两端使用同一个独立随机128位密钥，不能从UID、时间、固定密码或配对号推导。固件没有默认密钥。先完成原有UID配对，再通过**本地可信串口**逐端输入`IGNKEY <32个十六进制字符>`；不通过LoRa传钥，不回显密钥，不写Flash/TF，命令缓冲处理后擦除。重启、重新初始化配对后密钥清除，需要重新配置。`IGNKEY CLEAR`显式清除。

密钥操作遵守各端本地空闲条件：采集端须IDLE，遥控端须无在途事务；两端还检查各自供电与射频队列。满足条件才接受；接受后退休旧会话，提示CONNECT AGAIN。两端均设置好后，遥控STATUS→CONNECT建立新epoch。STATUS串口仅报告KEY SET RAM或KEY UNBOUND以及HW UNBOUND，不输出密钥。RAM临时配钥是本轮协议测试入口，不代表已实现生产设备密钥持久化或物理访问保护。

未配置密钥时不会发送认证请求，也无法对收到的新包作认证回复；接收的错误密钥、UID/方向/epoch、CRC、长度或字段包均丢弃，不改变结果缓存或采集状态。普通STATUS/ACK仍没有密码认证，不能替代点火认证回复。

## 9 当前固件的完整报文流程

### 9.1 上电和显式连接

1. 两板初始化ADC、TF、OLED及无线；无线初始化在原参数下打开高灵敏度LNA。GPIO预留保持原样，没有点火输出初始化。
2. 从原NV区恢复配对；不自动开始采集，不自动ARM/FIRE，也不自动发HELLO。未配对仍走既有本地INFO/NVINIT/PAIR流程。
3. 需要测新协议时，先在两端本地串口临时配置密钥；只测采集可不配置密钥。
4. 用户在遥控STATUS选择CONNECT：C发HELLO；A验证配对和持久计数、提交新epoch后回HELLO_REPLY。
5. C核对回显计数并发COMMIT；A激活会话并发STATUS。C收到有效递增STATUS后进入LINK_UP，新协议端点跟随同一epoch。
6. 握手丢包使用同一提议有界重试；重启、显式重新连接或ID耗尽须用新会话，不能恢复旧点火操作。

### 9.2 空闲和采集

- LINK_UP后A约每10s发STATUS；采集/TF/ADC新鲜标志等状态变化可立即发，电压细小数值变化等心跳。C只用递增STATUS刷新普通链路年龄。
- 用户在仅采集页START→确认：C发9B START；A回ACCEPTED ACK，开始日志准备；头写入/sync并进入CAPTURING后，再回同ID COMPLETED。
- 用户STOP：C发9B STOP；A先停入口并排空、写尾、sync/close，最终回同ID COMPLETED。文件错误则FAILED。STATUS不完成启停事务。
- START还没确认时按STOP，保留已有意图排序规则，用更高ID的STOP封锁晚到START。丢终态ACK可重发原采集命令取缓存；不会自动生成新START。
- 原始高频推力只写TF，LoRa不传完整原始流。认证查询不会修改采样、标定、日志或NV。

### 9.3 状态诊断和标定

- 0x05/0x06诊断在应用显式调用诊断API时发送/接收；当前STATUS菜单显示普通STATUS信息，不会自动轮询0x05。
- 用户进入CALIBRATION时，遥控标定模块按需发CAL_QUERY；菜单可见且无在途动作时，后台最多每心跳周期查询一次。
- 用户执行BEGIN/ZERO/REFERENCE/LOAD/SAVE/CANCEL等时，C发CAL_COMMAND，A回四片CAL_REPLY；每次四片完整且ID一致才更新视图。采集中拒绝改参数。
- SAVE超时只显示未知，通过后续查询活动修订核对；不能把收到请求或普通STATUS当作保存成功。

### 9.4 进入点火或联动菜单

- C已有密钥、普通状态新鲜、无采集事务或STOP意图时，进入这两个菜单自动发一次详细QUERY：0x0D、action0、selector1、target0。
- A认证成功后发一包0x0E主结果SNAPSHOT/DISABLED/UNBOUND，随后发一包0x0E详情。C允许乱序，收齐后显示HW NOT BOUND和OUT FEEDBACK UNKNOWN。
- 没有密钥显示KEY NOT SET，不发新包；未连接显示CONNECT REQUIRED。没有周期性点火广播或自动重连。
- START进入默认BACK的确认页。确认时当前profile=0，所以C再次查询真实状态，不捏造profile，也不发送ARM。客户端ARM接口已实现；其测试或后续调用发送ARM后，当前A只回REJECTED/UNBOUND。
- STOP发送认证DISARM；A记录幂等结果并回FAILED/UNBOUND，因为无法确认物理关闭。软件请求被处理与物理关闭必须分开理解。
- 当前没有FIRE菜单动作、长按触发链或点火GPIO。客户端FIRE接口只允许已有认证授权且未过期时创建一次请求；真实A从不发授权，因此现有整机无法进入这个路径。Host测试里的授权只用于验证协议，不能当实物授权。
- 联动菜单本轮只接入查询/取消协议，不自动运行“START采集→ARM→FIRE”。仅采集入口继续独立工作；完整联动需等独立硬件安全链和执行级设计完成。

### 9.5 超时、重试、取消和结果未知

- 重试以TX_DONE为计时基点；实际射频接受队列时才计尝试次数。普通原参数为ACK_RTO=500ms、操作总限120s、最多4次，抖动0～250ms。
- 快速认证回复一包；详细查询两包，等待窗口为两倍单回复RTO再加抖动。同一QUERY重试返回原冻结快照，不混入别的ID。
- QUERY、ARM、DISARM可有界重试原字节。FIRE一旦被射频接受，只允许一次发射，不自动重试；TX超时或丢回复转RESULT UNKNOWN，只能查询或DISARM。
- FIRE若尚未真正发射且本地保守授权截止已到，射频tick前移除排队副本。授权期限以该请求首次提交射频队列的时刻作保守基准，收到晚回复不能续期。
- DISARM可取代尚未发送的协议请求，并优先于尚未发射的普通请求；已在空中的包不能撤回。旧TX_DONE必须匹配旧包，不能错误完成刚创建的DISARM。
- 射频故障、会话退休、请求总限或重连使未完成结果未知，不自动ARM/FIRE，不清除物理风险。没有硬件反馈不能显示“输出已关闭”或“点火成功”。

## 10 精简收益与保持不变的部分

原R5草案的完整查询为29B请求+三个35B回复，共134B；R6为33B请求+两个35B回复，共103B，应用字节减少约23%。最大包仍35B，CRC、完整16B认证标签及ID规则均保留。

按当前参考SF10/BW812500/CR4/5，项目Semtech函数给出33B约80ms、35B约87ms；一轮命令及主回复约167ms，完整查询三包约254ms纯空中时间。处理、切换、BUSY与重试还会增加时延，不代表实测距离或响应保证。

LNA高灵敏度设置不改变SF/BW/CR或这些空中时长；芯片手册给出额外接收电流，实际模块、ADC噪声与距离仍须板测。本轮没有缩带宽、提高SF、改变功率/频点或降低ADC采样率。

## 11 实现与验证入口

- `Acquisition/src/st_ignition.c`：CMAC、两包编解码、协议专用UNBOUND服务端、客户端队列/重试/查询。
- `Acquisition/src/st_link2.c`、`st_link_runtime.c`：原会话与无线owner中的路由、发送确认、过期队列取消；旧包布局保留。
- 两端`board/reference_main.c`：各自静态协议上下文、本地临时密钥入口；没有点火GPIO或执行回调。
- `Controller/src/st_ui2.c`：仅点火/联动页面查询和DISARM；开始确认仍默认返回。
- `Models/test_ignition.c`：RFC4493四个向量、认证/坏包、未知硬件、幂等缓存、查询乱序、FIRE单次发送、本地密钥擦除和菜单隔离。
- `Models/test_link_runtime.c`：真实Semtech驱动软件事件模型中的双向认证收发、BUSY队列替换和授权过期FIRE在发射前删除。

复现：`python -B Models/run_host_tests.py`、`python -B Models/check_link_runtime.py`、`python -B Models/check_reference.py`、`python -B Models/build_reference.py --artifacts`。依赖沿用本地SDK_ROOT和ARM_GCC_ROOT；不自动烧录，不访问串口或探针。

硬件接入以后仍需单独设计：真实物理许可/急停、独立点火电源、反馈检测、输出默认禁止、独立截止、ARM一次性授权状态机、与记录run_id的联动和物理点火判据。不能把协议解析完成或一个软件“已绑定”标志当成这些条件已经满足。
