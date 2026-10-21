# OLED 与菜单来源核实

用户选择恢复原来常见 OLED。只读核实六轴机械臂项目的 `docs/UI_LOCAL.md`、`UI/Backends/ssd1306.c`、`BSP/board_config.h` 和硬件连接说明：软件为 SSD1306、128×64、I2C 七位地址 0x3C，硬件说明为 1.3 寸。驱动文件和板级地址相互吻合；没有凭尺寸推断控制器。没有发现应锁定的具体卖家料号。撤销 SparkFun/LCD-23453 等特殊采购建议；新板按3.3V供电及3.3V上拉核对实际模块。

SwiftThrust默认恢复 SSD1306/0x3C/无列偏移，G071候选PB8=SCL、PB9=SDA。SH1106独立profile只作为兼容分支，不会自动猜型号。实际模块批次、接线、电源/复位、I2C上拉仍待板测。

菜单参考原项目的编码器旋转选择、按下确认、上电按住先释放、确认页默认返回、各子页可见返回和简短状态栏。当前字体为本项目ASCII5×7，界面预览来自实际C帧缓冲；没有复制机械臂的硬件控制逻辑。

2026-10-21菜单统一：仅采集保留START、STOP、CALIBRATION、BACK；仅点火和点火并采集提供START、STOP、BACK。START确认后申请ARM，组合先等待采集进入记录；收到许可后进入独立FIRE确认，仍默认BACK，须旋转到CONFIRM FIRE再按下。STOP/BACK撤销准备并关闭MCU信号，组合另停止采集收尾。脉冲自然结束后组合继续记录。

ReferenceApp内置项目校验、自动连接和点火客户端。进入页面查询详情，空闲时最多1.5秒查询一次；许可有效期间不以查询覆盖授权。界面区分MCU OUTPUT LOW、ARMED、MCU PULSE ACTIVE、PULSE ENDED与RESULT UNKNOWN，外部反馈始终UNKNOWN。未附加协议客户端的独立组件预览仍显示N/A；实际菜单见[当前C帧缓冲预览](ignition_ui_preview.png)，字段与操作条件见[完整R7流程](LORA_PROTOCOL.md)。
