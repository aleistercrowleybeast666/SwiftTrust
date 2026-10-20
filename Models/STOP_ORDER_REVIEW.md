# START待确认时的STOP小修

2026-10-20，在73cef6cf交付基础上按用户授权修复“START待确认时不能提出STOP”和误显示NOT IMPLEMENTED。交付后冻结，等待用户上板测试反馈。未连接或烧录硬件，未改硬件参数、点火预留、采集格式或标定功能。

## 操作与协议边界

遥控器保留当前START身份，记录STOP意图。发送完成/无线发送超时解除当前发送等待后，必须有新鲜状态才生成同会话中序号更大的STOP。等待期间不再重发START。采集端只对“新STOP取消待完成START”开放例外：调用原有logger_stop，缓存新STOP序号与结果。STARTING或CAPTURING进入STOPPING；即使原START未到而STOP先到，IDLE上的STOP也建立新序号屏障。所有迟到旧START按旧命令拒绝，迟到旧START ACK不能完成新STOP。

重复STOP点击复用排队意图/事务，不分配新ID，不延长120s总时限；无线重试保留相同包。既有每轮4次、500ms加抖动等待、匹配ACK和总时限规则不变。STOP排队从首次点击开始计时。同步/关闭成功、logger回到IDLE后才有STOP COMPLETED；STATUS广播和ACCEPTED都不能结束事务。

失败、总超时、射频故障保留STOP未确认状态并阻止新START。链路恢复不自动重放；用户显式再按STOP重试同一STOP身份，或显式CONNECT更新会话。失败的CONNECT保留旧意图；成功CONNECT清除本地旧意图，板端原有重连收尾门控保持。普通无线信号缺失不新增自动停采策略。序号耗尽仍需显式重连，不复用序号。

界面区分STOP QUEUED、WAIT STOP CONFIRM、STOP UNCONFIRMED/STOP FAILED、BUSY、LINK LOST、STATE UNAVAILABLE与真正未实现的页面。按STOP仍使用现有单编码器，无新实体按键。

## 验证

- 完整Host：50组/54,398断言及Python导出检查通过。新增5组覆盖START回执迟到/丢失、STOP先到、STARTING/REC/源故障取消、重复STOP、存储同步失败、关闭被延迟、完成ACK丢失、已停、有限重试、队列超时、计时回绕、状态新鲜度、显式恢复与UI提示。停止集成使用真实client/server/logger和可控异步存储端口；既有完整Host还验证真实FatFs合成卡收尾。
- 无线运行时：7组/383断言通过。新增真实radio状态机软件模型覆盖START已排队/已上空、TX_DONE后STOP、迟到START ACK隔离、STOP射频超时重试、无线故障后的未确认状态。
- 补测“无线BUSY时迟到START完成ACK取消未发重发包”最初失败，断言为 `r.tx_pending && r.tx[0] == ST2_STOP && c.request.id == 2`（当时test_link_runtime.c第449行）。已取消的包不会再产生TX_DONE，修复为成功取消排队包时释放发送等待标志；同一回归现通过。
- 双板EIDE ReferenceApp实际ARM构建通过；155项官方SDK SHA256核对、强入口/IRQ、无未解析符号、镜像不覆盖NV区域均通过。F411：向量408B + 应用57,796B，RAM55,176B；G071：Flash49,080B，RAM9,736B。RAM含原有heap/stack预留。
- `firmware_host_validation.log`、`link_runtime_validation.log`、`reference_arm_validation.log`记录最终源文件/头文件哈希；本地 `dist/manifest.json`记录更新后的4个ELF/HEX的大小与SHA256。没有额外SDK、构建副本或输出目录。

## 交付后

两板应使用本次ReferenceApp一起升级。具体接线与NV保留规则仍以REFERENCE_APPLICATION为准。本轮未做物理无线、TF延迟或断电验证；完成提交推送后停止开发，等待用户实测反馈。
