# SwiftThrust

高速推力原始数据采集、TF日志、无线采集启停与状态显示项目。遥控器和采集器同仓库、各有独立EIDE项目；模型/规格/验证在Models。GitHub仓库按用户既有名称为SwiftTrust，本地项目名称保持SwiftThrust。

打开 `SwiftThrust.code-workspace`，EIDE分别选择 `SwiftThrust_Acquisition_LINK_CHECK` / `SwiftThrust_Controller_LINK_CHECK` 的LinkCheck目标构建。当前已通过真实ARM组件链接，入口仅CRC自检/WFI，**尚非可运行整板固件，烧录被禁用**。环境准备与未完成事项见[双工程说明](Models/EIDE_WORKSPACE_PLAN.md)和[交接](HANDOFF.md)。

当前无线协议无版本字段，12B状态、9B采集启停、10B ACK；完整字段及STLOG001见[协议](PROTOCOLS.md)。OLED恢复原项目常见SSD1306 128×64/0x3C规格。主菜单状态/仅点火/仅采集/点火并采集均可浏览返回；仅采集页有采集命令，两个点火页只有未实现说明。

无点火触发调用链、触发协议或输出驱动。未连接、烧录或访问任何现有机械臂/采集硬件。硬件、电压比例、时钟、任务、持久化及存储接线仍待确认，详见当前配置与验证记录。第三方源码保留原许可证；未为本项目擅自添加新许可。SDK、构建缓存和二进制不纳入仓库。
