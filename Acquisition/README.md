# Acquisition

STM32F411RCT6 + ADS1220 单通道采集端。实际入口为 `board/reference_main.c`，EIDE目标为ReferenceApp。8.192MHz HSE生成96MHz系统时钟，PA8 MCO1输出4.096MHz；SPI1通过DRDY/DMA获取24位AIN1-AIN2原始码，SDIO/FatFs记录STLOG001。PB10为链路LED，PC5不作ADC复位。

传感器参数与标定未有效绑定时允许原始记录。详见 [双板硬件契约](../Models/REFERENCE_APPLICATION.md)、[标定](../Models/CALIBRATION.md)和[交付报告](../Models/R8_HARDWARE_ADAPTATION_20261029.md)。输出ARM/FIRE独立确认、TIM2截止、R8协议和NV升级边界保持。没有实板验证。
