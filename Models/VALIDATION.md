# 验证边界

当前自动化：Host严格C11回归+Python日志导出恢复；两个实际EIDE工程由本机原生构建器编译链接；CubeMX保存的两份候选IOC与引脚表核对。报告分别为firmware_host_validation.log、eide_link_validation.log、host_validation.log，报告含时间或源码哈希。旧arm_object_validation.log为历史证据。

Host使用合成ADC、RF/I2C/SPI/存储/NV模型，覆盖坏CRC/格式、队列溢出、短写/故障、幂等重试/终态ACK丢失、握手旧epoch/持久化失败、状态超时、诊断快照乱序以及四页面无行为占位。C代码保持-Wall -Wextra -Werror -Wconversion -Wshadow -Wpedantic；原版第三方Semtech单独编译。EIDE完整HAL编译保留厂商设备裁剪后的unused-parameter警告，其余错误仍阻断。

EIDE入口只有CRC自检/WFI，无外设初始化；真实diskio/HAL端口未绑定实物时拒绝I/O。链接的代表性静态上下文/堆栈预留仅证明当前组件容纳在器件内存里，不证明最终任务栈/实时性。没有操作探针、串口、ADC、SD或机械臂。没有进行真实掉电、距离或电气测试。软件通过不等于Hardware PASS。

本阶段新增：43组/40,371条C断言；采用未修改FatFs R0.12c、真实worker/diskio、合成RAM FAT16走完整1500样本流程。NV内存模型穷举97个写入切断位置。HAL端口只经真实SDK ARM编译链接，不能把RAM盘、NV模型或编译通过当作真实卡/Flash/DMA测试。

标定核心原有5组回归：全部256字节参数CRC扰动、错误/取消旧值保留、配置/来源绑定、g/kg/N与重力、单编码器数字位/单位/过量程、1024字节逐点反色验证。calibration_ui_preview.png是实际C帧缓冲；未把模拟save、内存盘或编译通过称为实物标定/真实TF持久化。


最新标定集成：43组/40,371条C断言。真实FatFs合成卡逐扇区失败/部分写、启动恢复/配置拒绝、旧文件字节保持；短包→ADC队列→真实文件保存→活动参数→日志元数据→重挂载Host链通过。四片乱序/重复/CRC、未知结果核对、后台查询不冻结编辑均覆盖。实际ARM保留符号已检查，详见BOARD_BUILD_REVIEW.md。未板测，无实际SD掉电/射频/ADC时序保证。


无线owner收口：`python -B Models/check_link_runtime.py`仅测新增接线，6组/313断言，源码哈希见link_runtime_validation.log。旧43组完整Host报告保留e6589b0基线语义，不冒称本轮重复执行；当前ARM链接另更新并保留runtime入口。未板测。
