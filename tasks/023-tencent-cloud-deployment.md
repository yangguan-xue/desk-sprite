# TASK-023：腾讯云试部署与消息验收

- 输入：[SPEC-008](../specs/008-tencent-cloud-deployment.md)、现有 cloud/ 实现、SSH 主机 `tencent-118`。
- 状态：云端已部署，消息与安全验收通过；公网 MQTT 8883 连通待放行确认。
- 交付物：Dockerfile、锁定直接依赖的 requirements、Compose 配置、生产 Broker 配置、Nginx 片段、部署说明及真实部署记录。

## 执行顺序

1. 验证现有 Cloud/API/CLI 测试，构建独立镜像。
2. 服务器生成独立凭据、配置证书和 Broker，启动 Compose，确认健康检查。
3. 备份 Nginx，增加 HTTPS `/sprite/` 路径，校验并 reload。
4. 从 Mac 验证真实 HTTPS 与 MQTT TLS 收发、鉴权、幂等和 ACL。
5. 验证容器重启恢复；保存 Mac 云端配置，记录实际结果和限制。

## 可执行验证

```sh
.venv/bin/python -m pytest tests/cloud tests/cli -q
ssh tencent-118 'cd /opt/desk-sprite/app && sudo docker compose -f cloud/deploy/compose.yaml ps'
curl --fail https://voice-code.yangguanxue.top/sprite/health
```

通知验收须使用 runtime 中实际凭据，禁止将密码或 API Key 写入命令输出、部署记录或 Git。
