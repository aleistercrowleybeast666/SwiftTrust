# 验证边界

当前自动化：Host严格C11回归+Python日志导出恢复；两个实际EIDE工程由本机原生构建器编译链接；CubeMX保存的两份候选IOC与引脚表核对。报告分别为firmware_host_validation.log、eide_link_validation.log、host_validation.log，报告含时间或源码哈希。旧arm_object_validation.log为历史证据。

Host使用合成ADC、RF/I2C/SPI/存储/NV模型，覆盖坏CRC/格式、队列溢出、短写/故障、幂等重试/终态ACK丢失、握手旧epoch/持久化失败、状态超时、诊断快照乱序以及四页面无行为占位。C代码保持-Wall -Wextra -Werror -Wconversion -Wshadow -Wpedantic；原版第三方Semtech单独编译。EIDE完整HAL编译保留厂商设备裁剪后的unused-parameter警告，其余错误仍阻断。

EIDE入口只有CRC自检/WFI，无外设初始化或真实存储成功后端。链接的代表性静态上下文/堆栈预留仅证明当前组件容纳在器件内存里，不证明最终任务栈/实时性。没有操作探针、串口、ADC、SD或机械臂。没有进行真实掉电、距离或电气测试。软件通过不等于Hardware PASS。
