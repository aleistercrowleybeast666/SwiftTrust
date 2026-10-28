> 历史评审记录：本文中的旧ADC、时钟及引脚假设已由[2026-10-29双板硬件契约](REFERENCE_APPLICATION.md)替代；当前测试/交付见[R8硬件适配报告](R8_HARDWARE_ADAPTATION_20261029.md)。历史结果不代表当前固件或实板验证。

> 2026-10-20 更新：下文是 b539928d 之前的组件阶段记录，已由实际双板 ReferenceApp 替代；当前入口、参考参数、NV与接线以 [REFERENCE_APPLICATION.md](REFERENCE_APPLICATION.md) 为准。未上板验证仍然成立。

# 双EIDE工作区与构建

一个VSCode工作区可含两个独立EIDE工程。当前已创建实际4.1配置、独立name/uid、两个MCU的启动文件引用与链接脚本；控制器复用采集器的公共源码引用，不复制源码树。各目标输出相对本工程的build/eide/LinkCheck，互不覆盖。

本机已用已安装EIDE3.27.2的unify_builder3.11.1真实构建两端，见eide_link_validation.log。F411RCT6为256KiB Flash/128KiB RAM；G071KBT6非N版为128KiB Flash/32KiB RAM预算。代表性静态上下文与堆栈预留已参与链接，不是空项目；但LinkCheck入口只CRC自检/WFI，存储现为真实diskio与SDIO DMA适配，未绑定已核验卡时返回NOT_READY。它不是可运行采集固件。

配置外部环境变量：SDK_ROOT=共享SDK缓存，ARM_GCC_ROOT=Arm GNU Toolchain根目录，HOST_CC=Host GCC可执行文件。EIDE可将各工程.env.ini.example复制为env.ini并填写SDK_ROOT（实际文件名为`.eide/env.ini.example`与`.eide/env.ini`）。安装好的工具链由EIDE用户设置选择GNU Arm；不复制编译器到项目。

SDK清单为official_header_manifest.json，路径相对SDK_ROOT，含官方ST下载URL和SHA256，以及本机已有CMSIS Core/FatFs R0.12c文件的哈希。`python -B Models/restore_header_cache.py`只按需恢复ST必要小文件并校验；没有提供自动下载本机已有依赖的虚假保证，缺少的CMSIS/FatFs应按清单从合法来源提供。保留来源许可证。没有整包Cube下载或将SDK提交远端。

验证命令：`python -B Models/run_host_tests.py`（HOST_CC和SDK_ROOT，使用真实FatFs依赖）；`python -B Models/check_eide_link.py`；`python -B Models/validate_candidates.py`。后者还需要本机CubeMX数据库，CUBEMX_ROOT可指定安装位置。check_eide_link直接读取两份实际EIDE源清单，产物只存自动清理的临时目录。普通EIDE构建输出被.gitignore忽略。

当前Custom烧录入口只拒绝执行，不接触探针/串口。未来可分别配置两板的SWD烧录器/探针唯一ID后独立烧录；必须在整板目标和实际板型核验完成后启用，不能复用机械臂的烧录配置。UART独立于SWD，见DEBUG_UART.md。

官方多工程支持依据：[EIDE v3.26.9发布说明](https://github.com/github0null/eide/releases/tag/v3.26.9)及[ProjectExplorer源码](https://github.com/github0null/eide/blob/master/src/EIDEProjectExplorer.ts)。本机验证不代替硬件启动、实时性能或电气验收。

新增HAL/diskio与双槽记录的配置、IRQ和存储介质接入条件见[BOARD_INTEGRATION](BOARD_INTEGRATION.md)，当前LinkCheck仍不执行外设初始化。
