# 腾讯云试部署

契约见 [SPEC-008](../../specs/008-tencent-cloud-deployment.md)，执行任务见 [TASK-023](../../tasks/023-tencent-cloud-deployment.md)。

## 当前入口

- HTTPS API：`https://voice-code.yangguanxue.top/sprite`
- 健康检查：`https://voice-code.yangguanxue.top/sprite/health`，需同时确认 `mqtt_connected=true`。
- MQTT TLS：`voice-code.yangguanxue.top:8883`，需要腾讯云安全组允许入站 TCP 8883。
- API 只发布 `127.0.0.1:8200`；内部 MQTT 1883 不映射宿主。
- Docker 目录 `/opt/desk-sprite/app`，独立 Compose 项目 `desk-sprite`。

## 首次安装

前置：服务器安装 Docker Compose；现有 Nginx HTTPS 站点及该域名证书有效。
将 `cloud/app/`、`shared/`、`cloud/requirements.txt`、`cloud/deploy/` 中的部署文件上传至 `/opt/desk-sprite/app`。不要上传本地环境、缓存、测试凭证或 firmware/。

```sh
cd /opt/desk-sprite/app
sudo docker pull eclipse-mosquitto:2.0.22
sudo python3 cloud/deploy/init_runtime.py
sudo docker compose -f cloud/deploy/compose.yaml config --quiet
sudo docker compose -f cloud/deploy/compose.yaml up -d --build --wait --wait-timeout 60
sudo python3 cloud/deploy/install_nginx.py
```

`init_runtime.py` 在仓库外生成 API、Webhook、三个独立 MQTT 凭据；`cloud.env` 与 `credentials.json` 仅 root 可读；Broker 口令文件为加盐哈希，Broker 用户只读。重复执行沿用已有凭据；更新现有凭据需要同步密码文件与客户端配置，不能只改一个文件。
构建默认使用腾讯云 Python 镜像源，可用 Docker build 参数 `PIP_INDEX_URL` 覆盖；直接依赖固定于 requirements.txt。

Mac 配置在 `~/.sprite/config.json`，目录 0700、文件 0600。配置 `cloud_endpoint` 为上述 HTTPS API，`mqtt_broker` 为上述域名、端口 8883、`mqtt_tls_enabled=true`，使用系统信任库验证服务端证书。API Key 与 Mac 身份 MQTT 密码须从服务器安全传输，不可写进仓库或聊天记录。

## 验收

```sh
curl --fail https://voice-code.yangguanxue.top/sprite/health
```

公网 8883 可达后，从 Mac 执行：

```sh
.venv/bin/python cloud/deploy/verify_cloud.py --credentials ~/.sprite/cloud-trial-credentials.json
.venv/bin/python cli/bin/sprite notify '部署测试' 'Mac 发来的真实通知'
```

客户端验收凭据文件只保存 `api_key`、`host_macbook_pro`、`dev_esp32_c049ef`，权限 0600。服务器端完整凭据文件还有云端身份密码和 Webhook 密钥，无需传到 Mac。

如果云安全组阻止 8883，可先在服务器独立验证容器内使用临时 hosts 映射，验证真实 TLS 和证书域名匹配；这仅证明服务器内部链路，不证明公网 MQTT 可达：

```sh
sudo docker run --rm --network host \
  --add-host voice-code.yangguanxue.top:127.0.0.1 \
  --user 0:0 --cap-drop ALL --security-opt no-new-privileges \
  -v /opt/desk-sprite/runtime/credentials.json:/run/credentials.json:ro \
  -v /opt/desk-sprite/app/cloud/deploy/verify_cloud.py:/verify_cloud.py:ro \
  desk-sprite-cloud:trial python /verify_cloud.py --credentials /run/credentials.json
```

## 日常运维

```sh
cd /opt/desk-sprite/app
sudo docker compose -f cloud/deploy/compose.yaml ps
sudo docker compose -f cloud/deploy/compose.yaml logs --tail 100 api broker
sudo docker compose -f cloud/deploy/compose.yaml restart broker api
sudo docker compose -f cloud/deploy/compose.yaml up -d --wait --wait-timeout 60
```

两个服务配置自动重启，Docker 已启用系统开机自启。API 单 worker；事件与幂等缓存仍在内存中，重启会清空；Broker retained 消息保存在 Docker 数据卷。额度看板仍是 `is_mock=true` 的示例数据。

TLS 证书从 `/opt/certificate/voice-code.yangguanxue.top_bundle.pem` 和同目录 `.key` 复制到 runtime，并非实时挂载。原证书更新后执行 `sudo python3 cloud/deploy/init_runtime.py`，再重启 Broker。此次证书到期时间：2027-01-12 01:59:59 UTC（北京时间 09:59:59）。

## 回滚

1. 在目标 HTTPS server 块中删除 `include /etc/nginx/snippets/desk-sprite.conf;`，运行 `sudo nginx -t && sudo systemctl reload nginx`。首次安装前的备份位于 `/home/ubuntu/deploy-backups/voice-code-before-sprite-*.conf`；若后续已有其他站点修改，按上述方式移除本项目 include，避免覆盖新修改。
2. 在部署目录执行 `sudo docker compose -f cloud/deploy/compose.yaml down`，保留数据卷与 runtime。不要添加 `-v`。
3. Mac 如需恢复，使用 `~/.sprite/config.before-cloud-*.json` 备份覆盖 config.json，维持 0600 权限。
