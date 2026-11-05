# 2026-11-05 腾讯云试部署记录

## 实际部署

| 项目 | 结果 |
| --- | --- |
| 目标 | SSH `tencent-118`，腾讯云 Ubuntu 22.04 |
| 目录 | `/opt/desk-sprite/app`；凭据 `/opt/desk-sprite/runtime` |
| Compose | `desk-sprite`，API 与 Mosquitto 两个服务 |
| HTTPS | `https://voice-code.yangguanxue.top/sprite` |
| 健康 | HTTP 200，`code=0`、`mqtt_connected=true` |
| MQTT | 内部 1883 不映射宿主，TLS 8883 映射宿主 |
| 自动恢复 | 两服务 `unless-stopped`，Docker 已启用系统开机自启；实际重启两个容器后恢复健康 |
| Mac | 已备份并更新 `~/.sprite/config.json`，HTTPS 与 MQTT TLS 配置；文件 0600、目录 0700 |
| 凭据 | 服务端随机生成独立 API/MQTT/Webhook 凭据；客户端验收凭据在 `~/.sprite/cloud-trial-credentials.json`，0600；未写入 Git |

## 验证证据

- `.venv/bin/python -m pytest tests/cloud tests/cli -q`：**72 passed**，一个测试客户端依赖弃用提示。
- Docker 镜像在腾讯云构建成功；依赖使用腾讯云镜像源，解决公网 PyPI 下载慢的问题。
- 外网 HTTPS 健康检查返回 `{"code":0,"message":"healthy","data":{"version":"1.4.0","mqtt_connected":true}}`。
- 服务器内部实际使用 TLS 1.3 建连，校验证书链及 `voice-code.yangguanxue.top` 域名匹配，未跳过证书验证。
- `verify_cloud.py` 的真实 Broker 验收通过：设备身份收到通知；无效 Token、仓库测试 Token、无效 MQTT 密码被拒绝；同 Key 同 Body 不重复投递；变更 Body 返回 409；跨设备发布返回 MQTT 5 `Not authorized`（135）。
- 重启两容器并重新进行上述完整验收，全部通过。
- **Mac 经公网 HTTPS → 云端 API → 服务器内设备身份 MQTT TLS 订阅端**通过，事件 ID `evt_8ff6826cc62d4ba2`。订阅端是独立验证进程，未将其当作真实 ESP32 在线。
- 用户放行 TCP 8883 后，从 Mac 直接执行公网版 `verify_cloud.py`，全部检查通过：HTTPS 通知经云端转发，由 Mac 上的设备测试身份通过公网 MQTT TLS 收到，事件 ID `evt_e791e4f8f3354b74`；证书验证、鉴权、幂等与跨设备 ACL 均通过。
- 原站首页 HTTP 200。Nginx 校验通过；原配置已有重复虚拟主机名称警告，未因本次部署清理其他站点配置。

## 已解决的连通问题

最初公网 MQTT 8883 连接超时，服务器本机 TLS 正常。用户在云端放行端口后，Mac 公网验收通过，连通阻塞已解决；未直接读取云控制台具体规则。

## 范围限制

1. 公网验收使用独立测试进程，Mac Daemon 的实际运行尚未验证。
2. 后续同日实物联调已完成 ESP32 烧录、联网、物理弹窗与提示音，用户确认画面和声音正常；触摸交互仍禁用。详见[实物联调记录](../hardware/2026-11-05-cloud-trial.md)。
3. 额度引擎仍生成 `is_mock=true` 示例数据，不代表已经接入真实 Kimi 额度。
4. 事件存储与幂等缓存仍为单进程内存，重启丢失；Broker retained 数据使用独立持久化卷。

## 维护位置

部署/验证/回滚命令见 [cloud/deploy/README.md](../../cloud/deploy/README.md)。
首次 Nginx 备份：`/home/ubuntu/deploy-backups/voice-code-before-sprite-20261105-154010.conf`。
MQTT 证书到期：北京时间 **2027-01-12 09:59:59**，原证书续期后需同步 runtime 并重启 Broker。
