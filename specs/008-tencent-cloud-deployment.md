# SPEC-008：腾讯云试部署

- 日期：2026-11-05
- 授权：用户要求“你先部署上去试试”。
- 范围：现有腾讯云 `tencent-118` 上运行 Cloud Hub 与真实 MQTT Broker，验证 Mac 到云端到设备订阅端的消息链路。

## 部署契约

1. 独立 Docker Compose 项目 `desk-sprite`，目录 `/opt/desk-sprite/app`；运行凭据与证书放 `/opt/desk-sprite/runtime`，不进入 Git 或镜像。
2. API 仅映射宿主 `127.0.0.1:8200`。复用现有 `voice-code.yangguanxue.top` HTTPS，在现有 443 虚拟主机增加 `/sprite/` 前缀并剥除后转发。既有业务路径保持现状。
3. MQTT 内部监听 1883，仅供同一 Docker 网络服务使用，不映射宿主；对外仅 TLS 8883，复用该域名有效证书。证书必须验证，禁止跳过验证。
4. 三个独立随机 MQTT 密码与一个随机 API Key；沿用 `cloud_admin`、`host_macbook_pro`、`dev_esp32_c049ef` 及既有 ACL。GitHub Webhook 使用独立随机验签密钥。
5. 容器设置 `restart: unless-stopped`、日志轮转、内存上限；API 单 worker，避免内存事件存储和幂等缓存跨进程分裂。健康检查要求 HTTP 200 且 `mqtt_connected=true`。
6. Mac `~/.sprite/config.json` 保留其他用户配置，备份后改为 HTTPS 云端入口与 TLS MQTT；配置权限 0600、目录 0700。真实 ESP32 固件不在本任务中烧录。

## 验收与边界

- 云端容器健康、API 已连接真实 Broker。
- 外网 HTTPS `/sprite/health` 可访问；无效 Token 返回 401。
- 设备身份通过 TLS 订阅自己的告警 Topic；调用真实通知接口后收到相同事件 ID；重复请求不重复派发，换 Body 返回 409。
- MQTT 无效密码被拒绝；设备身份不能发布其他设备 Topic。
- 重启两个容器后恢复健康与消息投递。
- 记录 8883 外网可达性；若云安全组拦截，明确记录受限状态，不能冒充通过。
- 当前度量引擎仅提供标有 `is_mock=true` 的示例数据；本次部署不代表真实 Kimi 额度采集、物理屏幕/音效或触摸链路已经完成。
- 内存 EventStore 与幂等缓存重启会丢失，试部署接受该限制；Broker retained 数据持久化。

## 回滚与运维

保存现有 Nginx 虚拟主机备份于 `/home/ubuntu/deploy-backups/`，添加独立 include 后执行 `nginx -t`，成功才 reload；失败立即恢复备份。
回滚时移除该 include、校验并 reload Nginx，执行本项目 `docker compose down`；不删除 Broker 数据卷，不操作其他 Compose 项目。
更新证书后重新复制证书到 runtime 并重启 Broker；证书到期以实际证书日期为准。
