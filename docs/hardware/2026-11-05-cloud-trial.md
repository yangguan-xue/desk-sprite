# 2026-11-05 ESP32 实物云端联调

## 结果与证据

设备 MAC `d8:3b:da:4a:e9:84`，逻辑 ID `esp32_c049ef`。已刷入 Desk Sprite，用户确认测试通知的画面和提示音均正常。

| 检查 | 结果 |
| --- | --- |
| 写入前备份 | 完整 8,388,608 字节；esptool 设备端摘要校验通过 |
| ESP-IDF 5.1.2 构建 | 通过，应用 1,051,984 字节，factory 槽 3MiB |
| 宿主测试 | 6 个 C 测试程序通过；2 个 NVS Python 测试通过 |
| 写入 | bootloader、分区表、NVS、app 均通过烧录校验；未执行整片擦除或 eFuse 操作 |
| 联网 | Wi-Fi 获取 IP → SNTP 同步 → MQTT TLS 建连 → 收到订阅确认 |
| 指标 | 收到三行 retained 指标；仍是 `is_mock=true`，不是实际 Kimi 额度 |
| 通知 | 公网 HTTPS 下发，event_id `evt_cdb73e41eafd4854`；设备调度、绘制、提示音调用均匹配 |
| 用户实物确认 | 用户回答“画面和声音都正常” |
| 重启 | 再次正常启动、Wi-Fi/SNTP/TLS/订阅/三行指标恢复；串口已关闭 |

应用 SHA256：`f4129692bca858199a43d3ed617e0e606ae1a76d7e794c4a19bed6f47b7fcc24`。
原始日志、网络配置、NVS 镜像只在忽略的 `.local/device-cloud-trial/` 保存，目录0700、文件0600。软件验收字段见同目录 `acceptance.json`、`reboot-acceptance.json`，本记录保存去敏结果。

## 修复的实际阻塞

1. pySerial 默认 DTR 状态影响 USB 下载/运行模式。采集前显式设置 DTR/RTS=False，复位后收到应用启动证据，不以“复位函数返回”代替启动成功。
2. ESP-IDF 5.1.2 CSV NVS 生成器先计算 `len(str)`，后编码 UTF-8，中文 SSID 的记录长度与数据 CRC 不一致，设备拒绝加载该条目。`tools/provision_nvs.py` 校验字节长度后调用官方生成器的字节写入接口，回归测试检查真实分区条目的长度与 CRC；重新生成并写入 NVS 后实机成功读配置。
3. 原默认 factory 槽仅1MiB，完整 CA 包应用超过该空间；本轮显式采用3MiB槽，NVS固定0x9000/0x5000，PHY固定0xf000/0x1000。
4. 设备配置从 NVS 加载，移除固件内 Wi-Fi/MQTT 密码配置；不自动擦 NVS。MQTT 客户端先初始化，待 SNTP 同步后再启动 TLS。

## 构建、配置与回滚

在仓库根目录运行，ESP-IDF v5.1.2 已安装于以下位置。已有 `firmware/sdkconfig` 会覆盖 defaults；使用独立配置路径可避免复用旧配置：

```sh
. /Users/liruoxuan/esp/esp-idf/export.sh
idf.py -C firmware -B build-cloud-trial \
  -D SDKCONFIG="$PWD/.local/device-cloud-trial/sdkconfig.rebuild" \
  -D SDKCONFIG_DEFAULTS="$PWD/firmware/sdkconfig.defaults;$PWD/firmware/sdkconfig.cloud-trial.defaults" build
python firmware/tests/test_provision_nvs.py
```

私有配置 JSON 必须包含 `wifi_ssid`、`wifi_password`、`mqtt_username`、`mqtt_password` 四个字符串。不得把真实 JSON 写进 Git，也不要将原始串口输出粘贴到公开记录。生成器拒绝覆盖已有输出：

```sh
python tools/provision_nvs.py \
  --config .local/device-cloud-trial/device-config.json \
  --output .local/device-cloud-trial/nvs-new.bin
```

烧录前核对串口、MAC与备份，不使用 `erase_flash`。本轮备份：`.local/device-cloud-trial/before-trial.bin`，SHA256 `cd71ff44523dedc663334ada148599f307757eb200a3147007449b30a2c028a5`。回滚会恢复原 Kimi 固件及其当时配置，以下为回滚命令，**本轮未执行回滚**：

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem11201 --baud 460800 \
  --after no_reset write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 .local/device-cloud-trial/before-trial.bin
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem11201 \
  --after no_reset verify_flash 0x0 .local/device-cloud-trial/before-trial.bin
```

随后释放 DTR 并硬复位；采集日志确认真实应用启动，最后关闭串口：

```python
import serial
from esptool.reset import HardReset
p = serial.Serial(port=None, baudrate=115200, timeout=0.2)
p.dtr = False
p.rts = False
p.port = '/dev/cu.usbmodem11201'
p.open()
try:
    HardReset(p, uses_usb=True)()
    # 采集到私有文件并确认预期应用启动；不要直接输出网络配置。
finally:
    p.close()
```

## 当前边界

- 桌宠仍为静态占位图；触摸动作保持禁用；录音未实现验收。
- 本轮验证重启自动连接，未测试专门断网/断电恢复、Wi-Fi重新配网或长时间稳定性。
- 保留原 Kimi 固件未覆盖区域，但新分区不使用其文件系统；本轮不提供 OTA。
- 云端事件与幂等缓存仍在内存；MQTT证书续期后的同步和Broker重启仍需按部署文档维护。
