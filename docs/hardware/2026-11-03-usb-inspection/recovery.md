# 2026-11-03：小电视黑屏恢复记录

## 故障与定位

用户报告屏幕完全不亮。设备 USB 枚举正常、ESP32-S3 响应正常，Desk Sprite 应用持续输出填色日志，无连续崩溃证据。

用户指出同级 `kimi-pocket` 中有原项目资料，并确认黑屏前显示 Kimi 额度看板。该项目保存原厂完整备份及 SD2 看板镜像。QuotaClock SD2 参考屏幕引脚与当前固件不同，故板级配置不匹配是主要疑点；尚未完成 PCB 电气连通测量。

契约：[SPEC-006](../../../specs/006-device-firmware-recovery.md)；任务：[TASK-021](../../../tasks/021-device-firmware-recovery.md)。

## 恢复前备份

- 设备：`d8:3b:da:4a:e9:84`，ESP32-S3 rev v0.2。
- 本地文件：`../kimi-pocket/firmware_backup/desk_sprite_before_recovery_20261103_a.bin`（相对项目根目录）。
- 长度：8,388,608 字节；权限 600；目录受 Git 忽略。
- SHA256：`6399147127dd97c424cfea2b20febda5318621a0e94e3e8b94925ac56cac250e`。
- `verify_flash 0 <备份>`：完整 `0x800000` 字节 `verify OK (digest matched)`，已通过。
- 备份包括恢复前分区及持久配置，未打印其中 NVS 内容。

## 恢复镜像

- 来源：用户本地 `../kimi-pocket/firmware_build/kimi_pocket_sd2_s3_verified.bin`。
- 长度：8,388,608 字节。
- SHA256：`b80ab84c9d62ac119ca38e395f23266bfceb542859599597e012ecbd661645a6`。
- Bootloader 与 app 均为 ESP32-S3 镜像；两者 checksum / validation hash 均有效。
- 应用存量构建信息：Arduino / ESP-IDF v4.4.5，不是本项目 ESP-IDF v5.1.2 纯色测试程序。
- 写入地址：0；Flash mode / frequency / size 使用 keep，不重写镜像头参数。

| 分区 | 地址 | 长度 |
| --- | --- | --- |
| nvs | 0x9000 | 0x5000 |
| otadata | 0xe000 | 0x2000 |
| app0 | 0x10000 | 0x300000 |
| spiffs | 0x310000 | 0xe0000 |
| coredump | 0x3f0000 | 0x10000 |

## 验收状态

| 项目 | 结果 |
| --- | --- |
| 恢复前完整备份及设备端校验 | 通过 |
| 恢复镜像类型及完整性检查 | 通过 |
| 写入与写后设备端校验 | 写入时全片哈希校验通过；运行后除 NVS 外全部区域独立校验一致 |
| 应用启动 | USB 复位后从 Flash 启动；用户观察到 Kimi 看板 |
| 实物屏幕亮起 | 通过：用户确认 Kimi 看板恢复；75 秒监听结束、串口关闭后再次确认“仍然一直亮着” |
| 联网及 Kimi 额度刷新 | 待验收 |

### 校验引起的二次灭屏

用户首次确认 Kimi 看板亮起后，又报告灭屏。检查发现后续 `verify_flash` 整片比对失败，错误退出路径没有执行复位，设备留在下载模式。打开串口触发 USB_UART_CHIP_RESET 后，用户再次报告亮屏。诊断操作确实会短暂灭屏，但用户后来再次报告自动灭屏、轻触无反应，因此不能将所有灭屏都认定为同一个原因，也不能据此认定旧固件自行崩溃。

独立按区域核对结果见 [recovery-region-verification.json](recovery-region-verification.json)：bootloader/分区表、OTA 数据、完整应用槽、SPIFFS、coredump、剩余 Flash 均与恢复镜像一致；只有 NVS 的 4914 字节发生变化。运行固件可更新持久配置，运行后的全片镜像不能再要求逐字节与运行前完全相同。未输出或解析 NVS 凭证。

区域检查后的复位调用曾因 ESP32S3ROM 子类不接受 `uses_usb` 参数而失败，已立即以该类的无参数 `hard_reset()` 正确复位，命令返回成功并关闭串口。最终状态为已复位回应用，不再持续运行读写/校验命令。

### 后续稳定性观察

又读取了当前 NVS（仅 20KiB，私密备份存同级忽略目录），随后复位一次，保持串口连续监听 75 秒。全程只记录到开始观察时的一次 USB 复位启动，无额外重启、panic 或 brownout 日志；用户在这段观察期间回复“现在一直亮着的”。监听正常结束，串口已关闭，`lsof` 确认无进程占用该串口。这是限定时间内的观察结果，不能排除未复现的间歇故障。

恢复镜像的 NVS 初始为全 FF，运行后写入默认配置；它并非完整保存了用户最后的网络及看板设置。原操作前的完整备份中仍留有旧配置，新 NVS 与旧配置均已保留，尚未再次刷入。真实额度联网刷新未验收。

用户最终确认串口关闭后“仍然一直亮着”。本次亮屏恢复已通过；间歇灭屏在最终观察期间未复现，不能仅凭这次观察认定其全部根因已查明。

## 回滚方式

如需恢复到本次操作前，可将上述当前固件完整备份以 `write_flash --flash_size keep --flash_mode keep --flash_freq keep 0 <备份>` 写回，再用 `verify_flash` 校验。它恢复的是黑屏时的纯色测试程序，不是原厂界面。

原厂镜像仍保存在同级 `kimi-pocket/firmware_backup/esp32s3_factory_backup_8mb.bin`，本次恢复选择 Kimi 看板镜像。
