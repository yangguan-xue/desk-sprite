# 2026-11-05 樱花汽水界面烧录

- 用户授权：将刚推送的页面烧入 USB 实机。
- 页面来源：`origin/codex/sakura-display`，`8123007`；合入当前云端联调分支，集成提交 `30d1d59`。唯一冲突位于页面渲染函数，采用新渲染器并保留原通知绘制日志，NVS 配置和 MQTT TLS 连接逻辑保留。
- 实机：ESP32-S3，MAC `d8:3b:da:4a:e9:84`，串口 `/dev/cu.usbmodem11201`。
- 构建：ESP-IDF 5.1.2，通过；应用 1,128,560 字节，3MiB 应用分区剩余 64%。编译时尚未创建集成提交，镜像版本标签为 `b6fe38d-dirty`，应用源码与集成提交一致，以以下 SHA256 标识实际镜像。
- 应用 SHA256：`af2ad4587caeb8814c8c642e2b81f75cfefa91b29baf16b69ae024ebd09fcedf`。
- 软件验证：共享渲染器测试及 ASan/UBSan 通过，10 种状态导出通过；模拟器 headless 构建及运行通过。本地模拟器未连接 MQTT Broker；实际 MQTT 验证使用下面的实机结果。
- 写入前完整 8MiB 备份：`.local/device-cloud-trial/before-sakura.bin`，SHA256 `b42dadf204e5502942c7f2ef26b926c3697a25498677c400f9596e70af7206c9`，设备端 `verify_flash` 摘要校验通过。现有分区表与构建结果一致。
- 仅写入 `0x10000` 应用区，esptool 报告 `Hash of data verified`；保留原 NVS 和 bootloader，未整片擦除。
- 重启后收到 Wi-Fi 获取地址、SNTP 同步、MQTT TLS 连接和订阅确认。
- 测试通知 `evt_973857c897fc4214`：匹配 `Alert rendered` 与 `Alert tone completed` 日志，通知时长 8 秒；串口已关闭。
- 随后再次打开串口观察 15 秒：产生一次 `USB_UART_CHIP_RESET`，应用重新启动并恢复 MQTT 订阅，无 panic、栈溢出、LCD 超时或重复启动记录。该观察证明串口复位后可恢复，未将其计为无复位连续运行测试。
- 实机画面外观和听感尚未由用户确认；日志中的绘制与提示音调用不代替目视、听感验收。

私有构建、备份、写入日志位于 `.local/device-cloud-trial/sakura-*.log`，启动与通知证据位于 `.local/device-cloud-trial/sakura-check/`。备份及日志按私有文件保存，不提交 Git。

需要回退时，先核对设备 MAC，再将上述完整备份写回 `0x0` 并校验；复位时按[云端联调记录](2026-11-05-cloud-trial.md)显式释放 DTR，确认应用启动后关闭串口。
