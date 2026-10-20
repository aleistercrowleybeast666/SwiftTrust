# 首次START / 取消再启动回归

2026-10-20。仅离线测试，没有连接硬件。应用准备逻辑由F411 `reference_main.c` 实际调用的 `st_ref_prepare_start` 提供；回归经真实 `st2_server_receive`、`st_cal_endpoint_port`、`st_cal_apply_logger`、`st_logger_start` 生成STLOG001头。文件提交/完成由有界内存端口模拟，TF硬件可靠性不在此测试中。

旧顺序：先调用校准回调、后设置clock_verified。实测测试退出1：`cal integration line392: f->logger.header[59]&1u`。准备回调因此拒绝应用已保存系数，而START仍按raw启动。红态源码SHA256：`65d8d76f0ad6e6dc0c8619203ae1a679080ec3a91e26a4a9ac21c7487854e350`；红态测试SHA256：`ada5839bf8392a7fdfbbbf80443a220f4e8b4de34818c35d13262c3abf7302e9`。

修复顺序：板级就绪/空闲门控通过后先设置epoch和clock_verified，再调用校准端点。首个START的已校准位、profile ID/revision、zero、斜率、线数、epoch及头CRC全部核对。未确认源、采集中或校准向导占用时仍拒绝；缺少传感器配置时明确清除calibration_valid并保留raw-only。

取消回归随后发现第二个实际问题：`reference command type=2 id=5 result=1 state=0: next explicit START rejected after canceled START drained to IDLE`。原协议SHA256：`9121426c298d02bf7fdb11227aac8b5ce5429d03d770614f87abf8dcdf27fb9d`。修复finish_command：未进入CAPTURING就被取消的START，日志排空回到IDLE时变为FAILED，避免永久ACCEPTED占用后续事务。

通过内容：已有系数首次START→写头→采集→STOP收尾；标定草稿BEGIN/CANCEL保持原系数；STARTING阶段源/重连取消并收尾；再次显式START仍应用原系数；raw-only分支。测试按真实owner节奏推进logger和server_poll，保持一条在途射频命令，不新增STARTING阶段的并行命令功能。

最终结果：45组/54,109个C断言及Python日志检查通过；无线owner6组/313断言、参考NV模型5组/75,564检查通过；最终双板ReferenceApp ARM构建通过，镜像与哈希已刷新。见firmware_host_validation.log、link_runtime_validation.log、reference_host_validation.log、reference_arm_validation.log和本地dist/manifest.json。

最终关键源码SHA256：
- `Acquisition/src/st_reference.c`: `31179f0b06cfc9877d62b403c94c3b199b24041623884e944182f0e00acdfc86`
- `Acquisition/include/st_reference.h`: `bda4a0aa148e99de43f71c6909ce080f8165d506dfa04e5d1294b953f9536569`
- `Acquisition/board/reference_main.c`: `951739503675f10219b6c843795b4b7e88c254eee5b56b71b17a39bfb98f0585`
- `Acquisition/src/st_link2.c`: `f39f412d7bcc800faf0fd1d7c9399884d2bfbbd2f101eb82554c5a8c9737fd2e`
- `Models/test_cal_integration.c`: `26b12c0e25eb807bcea087e32a261a1cfa033d03f116a7fa4db788448bd9682d`
