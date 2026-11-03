# 独立 RAM 板级诊断

遵循 [SPEC-007](../../specs/007-board-hardware-validation.md)。只适用于本次已核验的设备，引脚见 [板级记录](../../docs/hardware/2026-11-03-usb-inspection/board-validation.md)。RAM运行不替换Flash中的Kimi镜像，连接中断或复位会丢失诊断程序。

```sh
. /Users/liruoxuan/esp/esp-idf/export.sh
idf.py -C tools/board_probe -B /tmp/desk-sprite-board-probe-build \
  -D SDKCONFIG=/tmp/desk-sprite-board-probe-sdkconfig build
python tools/board_probe/run_ram.py /tmp/desk-sprite-board-probe-build/board_probe.bin \
  --port /dev/cu.usbmodem11201 --output /tmp/board-probe-result.json
```

不要使用构建输出建议的`flash`命令。`run_ram.py`先拒绝含Flash映射段的镜像，再调用ROM的RAM加载命令；全部退出路径尝试复位并关闭串口。连接实际已断开时，发送复位命令不等于确认固件已启动，应根据设备画面确认。

结果中的`probe_complete`只表示收到诊断结束标记；`hard_reset_command_completed`表示复位方法返回。脚本不验证应用启动，因此`flash_application_start_verified`保持false，需另行记录用户观察。诊断异常、复位异常和关闭异常分别保存，诊断或清理未完成时返回非零退出码。早期结果的`reset_to_flash`也只代表复位方法返回。

当前程序读取ES8311身份、显示Mode3彩条/8px白框/底部紫青黄白色带，然后仅配置GPIO5/TOUCH_PAD_NUM5原生电容测量，每100ms记录原始值、60秒后复位。依据是已取得的卖家S3原理图TP_KEY直连IO5；不遍历配置其它通道。先保持不碰5秒，再按住顶部2秒/松开3秒三次。当前`audio_probe(false)`不重复发声；此前低幅度单声已由用户验收。

诊断曾用Mode0出现黑屏，现已按旧TFT_eSPI驱动修正。下边框重测已由用户照片确认粗框与底部四色带。早期被动数字输入测试不足以识别原生触摸；新GPIO5电容测量600次读取完成，但尚未建立可靠按下/松开关联，产品触摸继续禁用。原理图、屏幕型号对照和本次实测见[卖家资料核对](../../docs/hardware/2026-11-03-usb-inspection/seller-materials-review.md)。诊断应在连接稳定后运行。

触摸初始化参考本地ESP-IDF v5.1.2官方`examples/peripherals/touch_sensor/touch_sensor_v2/touch_pad_read/main/tp_read_main.c`（CC0-1.0），仅启用通道5，未启用额外降噪/屏蔽通道。使用`soc/touch_sensor_channel.h`并断言通道5对应GPIO5，避免使用SDK中遗留`soc/touch_channel.h`的不同映射。

寄存器初始化序列参考QuotaClock `defb2c83986de0bf88b826a716f0d0415315d2f4` 的`clock_rom/mono_audio.h`，其标注Apache-2.0及Espressif来源（Copyright 2015-2026 Espressif Systems (Shanghai) CO LTD）；原始驱动见[esp-bsp ES8311](https://github.com/espressif/esp-bsp/tree/67f41a169401d1f23e827188dd08fb351cb0febe/components/es8311)。身份寄存器FD/FE/FF的含义由官方`priv_include/es8311_reg.h`交叉核对。等待时序和SPI模式参考匹配的TFT_eSPI配置，链接在板级记录中。
