# Models

本目录包含离线回归、硬件契约、日志导出工具和交付证据。当前版本入口见 [REFERENCE_APPLICATION.md](REFERENCE_APPLICATION.md) 与 [2026-10-29适配报告](R8_HARDWARE_ADAPTATION_20261029.md)。`hardware_*_20261029.json`来自本轮epro2；`reference_pins.csv`为双板引脚契约。

主机测试：`run_host_tests.py`（英文加`--english`）、`check_link_runtime.py`、`check_reference.py`、`check_ignition_f4.py`、`check_ads1220_capture.py`、`check_hardware_contract.py`、`validate_candidates.py`。目标编译：`build_reference.py --artifacts`。导出日志：`export_stlog.py <BIN> <新CSV>`，兼容历史双通道及ADS1220单通道STLOG001。

文件名带v0.1、20261024或历史评审时间的资料保留为历史依据，不定义当前硬件。所有模型测试都不代替实物电气、射频、持续吞吐或执行输出安全验证。
