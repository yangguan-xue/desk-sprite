# TASK-024：ESP32 云端实物联调

- 输入：[SPEC-009](../specs/009-device-cloud-trial.md)、USB 实机、已部署云服务与私有设备凭据。
- 状态：完成（2026-11-05，用户确认画面和声音正常）。
- 交付：NVS 配置读取、分区及必要连接修正、私有可回滚备份、实机去敏验收记录。

## 顺序与验证

1. 备份并校验完整 Flash；确定设备身份、现有网络配置可用性。
2. 先测试 NVS 缺项/超长输入，再实现读取；构建不含凭据的固件，生成私有 NVS 镜像。
3. 运行宿主 C 测试、ESP-IDF build；核对镜像和分区大小。
   - 实机发现中文 SSID 的官方 CSV 生成器长度错误；添加 UTF-8 字节写入工具与长度/CRC 回归测试后重置 NVS。
4. 刷写并验证；捕获私有串口日志，确认 Wi-Fi/SNTP/MQTT 上线。失败恢复备份或修复明确根因。
5. 发送公网 HTTPS 通知，匹配实机 event_id；重启验证重连，向用户请求目视/听感验收。

```sh
. /Users/liruoxuan/esp/esp-idf/export.sh
idf.py -C firmware build
.venv/bin/python cloud/deploy/verify_cloud.py --credentials ~/.sprite/cloud-trial-credentials.json
git diff --check
```

## 验收

- ESP-IDF build、6个宿主C测试与2个NVS生成测试通过；完整备份及写入校验通过。
- 实物 Wi-Fi/SNTP/MQTT TLS/订阅/三行模拟指标通过，HTTPS 通知 `evt_cdb73e41eafd4854` 匹配绘制与提示音日志，用户确认画面和声音。
- 重启后再次连接并收到指标，串口关闭。
- 去敏证据、构建与回滚说明：[实物联调记录](../docs/hardware/2026-11-05-cloud-trial.md)。触摸、真实额度和动态桌宠仍未完成。
