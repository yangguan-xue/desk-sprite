# TASK-001～020 实现审查

- 日期：2026-11-03
- 范围：`tasks/tasks.md` v1.1.0、SPEC-001～004 v1.4.0，以及 `dc3644d` 之后的实现；已检查至 `a3c9214`（包括审查期间新增的构建忽略规则及 8MB Flash 配置）。
- 结论：**Request changes，暂不通过。不能按 20 项全部完成验收。**
- 共 13 项：7 项 P1（阻塞核心功能或存在权限问题），6 项 P2（应修复的功能、资源及验收缺口）。
- 本次只新增审查报告，没有修改实现代码。已有 `assets/` 未跟踪内容未改动。

## P1：优先修复

### R01：默认应用启用了公开测试凭证

- 对应任务：TASK-004；安全边界。
- 位置：`cloud/app/services/auth.py:24-37`、`cloud/app/main.py:23-26`。
- 问题：正常启动的应用直接注册 `spr_live_test_key`、`spr_live_guest_key`；未设置环境变量时，Webhook HMAC 密钥回退到公开字符串 `test_webhook_secret`。这些字符串是有效凭证，不只是配置示例。
- 影响：默认部署允许外部调用者直接通过鉴权，或生成能够通过验证的 GitHub Webhook 签名。
- 验证：默认 API Key 请求返回 HTTP 200；使用默认 HMAC 密钥签名的 Webhook 返回 HTTP 200。
- 修复方向：正常运行必须加载真实凭证，缺失时拒绝启动或关闭相应入口。测试凭证通过 fixture 显式注入，不进入默认应用注册表。

### R02：通知调用者可指定未经授权的目标 Mac

- 对应任务：TASK-004、TASK-005；SPEC-002 的设备与宿主绑定。
- 位置：`cloud/app/api/v1/notify.py:95-105`，后续使用点 `cloud/app/services/action_router.py:149-152`。
- 问题：鉴权只校验 `target_device`，随后直接把请求中的 `target_host` 存入 EventStore。动作路由信任该记录，并将浏览器指令发送到对应宿主 Topic。
- 影响：访客可以给自己的设备发送绑定其他人 Mac 的交互通知；接通 MQTT 后，点击访客设备可导致其他人的 Mac 打开访客指定的 URL。
- 验证：使用 guest Key，指定 `target_device=esp32_guest`、`target_host=macbook_pro`，HTTP 200，记录中的目标宿主确为 `macbook_pro`。
- 修复方向：从可信设备绑定中派生宿主，或对显式覆盖执行宿主授权校验；加入跨宿主拒绝用例。

### R03：云端主入口没有接入实际 MQTT 运行链路

- 对应任务：TASK-004～006。
- 位置：`cloud/app/main.py:21-26`、`cloud/app/services/mqtt_service.py:28-35`、`cloud/app/api/v1/notify.py:141-143`。
- 问题：`create_app()` 没有创建或挂载 MQTT 服务，也没有连接、订阅及网络循环。MQTTService 自身始终将 `_client` 初始化为 None，没有构建连接的方法；`publish()` 只记录内存列表。通知接口在服务缺失时跳过发布，仍返回成功。
- 影响：默认 uvicorn 应用收下通知却不会送达设备；设备动作与 Mac 状态也没有实际入口。MetricsEngine、LifecycleMonitor 没有运行期组装或周期调度，`/up/status` 也没有被 dispatch_message 分发。
- 验证：默认应用 `hasattr(app.state, "mqtt_service")` 为 False；运行时代码中 `push_metrics`、`handle_status_message`、`check_offline_timeouts` 只有定义，没有调用。
- 修复方向：通过应用 lifespan 组装真实 MQTT 客户端、路由、状态及度量服务，管理连接与后台任务；不可用时不能静默返回“已成功下发”。

### R04：daemon 入口调用不存在的 API，启动即退出

- 对应任务：TASK-010、TASK-012。
- 位置：`cli/bin/sprite-daemon:59-69`、`:86-89`。
- 问题：`SpriteConfig.load()` 不存在，实际为模块函数 `load_config()`；AppMonitor 参数应为 `on_mode_change`；ScreenMonitor 暴露 `is_locked` 属性；AppMonitor 使用 `poll_tick()` 与 `set_screen_locked()`。入口使用的这几处名称均不匹配。
- 影响：启动立即抛出 AttributeError，MQTT 订阅、锁屏检测与指令执行均不会运行。PID 文件在报错前写入，清理 finally 尚未进入。即使仅修复方法名，入口仍没有启动 ScreenMonitor 或接入锁屏回调，无法满足锁屏立即切换 sleep 的要求。
- 验证：调用实际 `run_daemon()`，复现 `AttributeError: type object 'SpriteConfig' has no attribute 'load'`。
- 修复方向：统一入口与组件 API，接通 ScreenMonitor 生命周期及回调，把初始化失败纳入清理；以真实入口做启动及退出测试。

### R05：终端动作报文与云端协议不兼容

- 对应任务：TASK-016、TASK-019。
- 位置：`firmware/src/engine/action_reporter.c:20-22`、`:57-65`；`firmware/sim/sim_mqtt.c:175-183`。
- 问题：终端生成 `act_<timestamp>_<counter>`，发送平铺的 `action_type`、`active_event_id`、`timestamp`。云端要求 UUIDv4、`created_at`、`expire_at=created_at+10`，以及嵌套的 `data.action`、`data.active_alert_id`。
- 影响：实际动作全部被云端作为 malformed_payload 拒绝，反向浏览器闭环不可用。另有两处需一起处理：`device_action_t.action_id[36]` 放不下 UUID 加终止符；模拟器鼠标点击未填当前告警 ID（`firmware/sim/main.c:112-122`），修正 JSON 后仍无法定位交互事件。
- 验证：将 C action_reporter 实际生成的报文交给 DeviceActionPayload 校验，`action_id`、`created_at`、`expire_at`、`data` 全部报错。
- 修复方向：统一 C 结构和序列化为 SPEC-002 格式，模拟器复用相同上报器，携带活跃告警 ID；用真实 C 输出做云端契约测试。

### R06：模拟器收到告警直接弹窗响铃，绕过调度引擎

- 对应任务：TASK-013、TASK-015、TASK-018、TASK-020。
- 位置：`firmware/sim/main.c:24-30`、`firmware/sim/Makefile:4`。
- 问题：on_alert 无条件调用 show_alert 与 play；构建根本未包含 alert_filter、alert_queue、touch_button、action_reporter，主循环也不推进告警倒计时。on_state 只更新状态栏，没有改变调度模式。
- 影响：meeting 仍播放提示音，sleep 下 medium 仍弹窗，low 也弹窗；medium 不会在规定时间自动关闭，高优先级告警可被后来的 medium 覆盖，队列和去重均不生效。
- 修复方向：让真实消息入口和主循环接入共享过滤、排队、出队复核及手势状态机，按调度结果驱动 UI 与音频；验收应从 MQTT 入口观察实际显示和声音事件。

### R07：物理固件仍为日志桩，TASK-017 尚未实现

- 对应任务：TASK-017；影响物理设备验收。
- 位置：`firmware/main/main.c:17-26`；`firmware/main/hal/st7789.c:4-11`、`es8311.c:4-15`、`ttp223.c:4-10`。
- 问题：屏幕和音频函数只 printf，触摸读取恒为 false；入口打印初始化日志后返回。没有实际 SPI/PWM/I2C/I2S/GPIO 初始化，没有 Wi-Fi、ESP-MQTT、LVGL 渲染与缓冲、运行任务及重连链路。
- 影响：这些文件即使成功编译，也无法完成屏幕、声音、触摸或网络功能。这是实现缺失，不能归因为“缺硬件尚未验证”。
- 修复方向：实现实际 ESP-IDF 外设与网络驱动，集成共享引擎；分别提供 idf.py 构建证据和真实硬件验收记录。若当前只交付软件模拟，应明确保留 TASK-017 未完成状态。

## P2：功能及验证缺口

### R08：MQTT 配置和客户端未实现 TLS

- 对应任务：TASK-001、TASK-012；SPEC-001 的 TLS + ACL 架构、SPEC-004 的 8883 配置。
- 位置：`cloud/deploy/mosquitto/mosquitto.conf:4-7`、`cli/sprite/daemon/mqtt_worker.py:36-45`。
- 问题：Broker 仅监听明文 1883，未配置 TLS 证书；Mac 客户端只设置账号密码，从未启用 TLS，模拟器也使用明文连接。
- 影响：按当前配置连接时凭证和消息走明文；按规范把端口设为 8883 并不能使客户端完成 TLS 握手。
- 修复方向：添加 Broker TLS 监听、客户端证书校验及 CA 配置，实测正确证书可连接、无效证书被拒绝。

### R09：真实指标未解析和渲染，逐行过期信息也丢失

- 对应任务：TASK-014。
- 位置：`firmware/sim/sim_mqtt.c:100-107`、`firmware/sim/main.c:33-36`、`:169-190`；`firmware/src/include/sprite_types.h:83-102`、`firmware/src/ui/ui_metrics.c:14-23`。
- 问题：metrics 只读取时间字段，没有解析 `data.bars`，item_count 一直为 0；回调只打印日志，SDL 固定绘制 90/140/170 像素进度条。C 类型把 sampled_at/ttl_sec 放在整个 payload 上，协议要求每条 bar 单独携带。
- 影响：云端额度变化不会出现在界面上，也无法正确表达“一行过期，其他行正常”的 STALE 状态。
- 验证：将真实 MetricsEngine 生成的三行消息送入 C sim_mqtt_handle_message，回调中 item_count 为 0。
- 修复方向：按协议逐条解析数据与时间属性，接入 ui_metrics，再由真实渲染模型绘图。

### R10：过期记录没有周期清理，内存并不受 TTL 限制

- 对应任务：TASK-003、TASK-004。
- 位置：`cloud/app/services/event_store.py:53-69`、`:83-94`；`cloud/app/services/idempotency.py:85`。
- 问题：save 只增加记录；get 只清理被查询的那一个事件。虽然实现了 cleanup_expired，但应用运行路径从未调用。幂等缓存也没有全局清理调度。
- 影响：未被点击或查询的通知、持续使用不同键的幂等请求会永久占用内存；24h/5min 只是访问时的逻辑失效，并没有实现有界存储。
- 修复方向：接入周期清理或在写入时执行有界清理，并验证无需再次查询旧 ID 也会释放历史记录。

### R11：非 TTY 包装器只转发给直接子进程，遗留后台后代

- 对应任务：TASK-009。
- 位置：`cli/sprite/commands/run.py:132-145`。
- 问题：非 TTY 分支没有为被包装命令创建独立进程组；信号处理使用 os.kill(pid)，只影响直接子进程。
- 影响：包装 shell、构建脚本等会产生后代的命令时，终止 wrapper 可留下仍在运行的任务；后代还可能持有管道 FD，导致流水线迟迟不结束。
- 验证：非 TTY 包装含 sleep 后代的 shell，发送 SIGTERM 后 wrapper 返回 143，但 sleep 仍存活；探针结束后已清理后代进程。
- 修复方向：建立独立子进程组并向该组转发信号，确保不影响 wrapper 所在父进程组；覆盖 shell 后代退出场景。

### R12：CI 尚未完成就被映射成高优先级失败通知

- 对应任务：TASK-004。
- 位置：`cloud/app/services/webhook_verifier.py:65-74`。
- 问题：workflow_run 只判断 conclusion 是否等于 success，没有过滤 action 或完成状态。requested/in_progress 等事件的 conclusion 为 null，也会进入失败分支。
- 影响：一次正常启动的 CI 会触发 high + alert_beep；按高优先级规则还会持续占据弹窗直到用户确认。
- 验证：转换 `action=in_progress, conclusion=null`，结果为 `priority=high, sound=alert_beep`。
- 修复方向：仅对完成事件生成结果通知，并明确处理 cancelled、skipped 等结论；未完成事件成功接收后忽略。

### R13：E2E 测试没有验证实际端到端行为

- 对应任务：TASK-018～020。
- 位置：`tests/e2e/test_notification_flow.py:24-27`、`:78-83`、`:108-113`、`:138-142`；`tests/e2e/test_security_and_edge_cases.py:187-210`。
- 问题：MQTT 用 MagicMock 替代，终端行为通过测试内自行赋值或重写 Python 条件判断“验证”。例如 meeting 用例直接写 force_mute=True 再断言为 True；coding 用例断言自己刚赋值的常量。反向测试直接串调用 ActionRouter 与 CommandExecutor，未经过模拟器、Broker 或 daemon 入口。
- 影响：这些测试可以在 R03～R06 均存在时通过，无法满足任务明确要求的软件完整闭环、倒计时、静音、队列及真实动作报文验收。
- 修复方向：启动隔离的真实 Broker、API、CLI/daemon 与 C 模拟器，观察实际报文、音频调用及 UI 状态。浏览器打开等操作系统外部副作用可替换，但应保留消息与进程边界。
- 另有测试本身的竞态：`test_security_and_edge_cases.py:176-180` 在 waitpid 前关闭 PTY master，可使会话收到 SIGHUP。本次该断言失败，不能据此直接认定生产同步屏障触发了 SIGTTIN；应先修正测试终端生命周期。

## 实际验证结果

| 验证 | 结果 | 能证明的范围 |
|---|---|---|
| Python `tests/` | 65 通过，1 失败 | 失败为上述 PTY 测试；现有通过用例不等于真实 E2E 通过 |
| C 指标、调度、手势三组测试 | 10 个测试函数通过 | 独立 C 模块行为，不证明模拟器已接入这些模块 |
| SDL2 `make test_build` | 编译与 headless smoke 通过 | 本机未检测到 libmosquitto，使用 standalone 模式，无真实 MQTT 连接 |
| ACL generator `--test` | 通过 | 规则生成自测，无真实 Broker 越权发布验证 |
| 默认应用、跨宿主、daemon、C/Python 协议探针 | 复现上述问题 | 使用实际应用或模块入口 |
| ESP-IDF、真实板卡、TLS Broker 联调 | 未验证 | 缺少对应工具或硬件；不能声称通过 |

Python 测试命令（使用临时依赖环境，未向仓库添加依赖文件）：

```sh
uv run --no-project --with pytest --with fastapi --with 'pydantic>=2' --with httpx --with paho-mqtt python -m pytest tests -q
```

C 测试编译时使用 `-I firmware/src/include`。tasks 中三条 C 验证命令给的是 `-I firmware/src`，需要同步修正，否则直接复制任务命令会找不到头文件。

## 建议修复顺序

1. R01、R02：关闭默认测试凭证与跨宿主权限缺口。
2. R03、R04、R08：先让真实云端与 Mac daemon 能安全启动、连接并交换消息。
3. R05、R06、R09：对齐终端协议，接入共享状态机和真实指标渲染。
4. R10～R12：补齐淘汰、进程退出和 Webhook 事件分类。
5. R13：以真实 Broker 和 C 模拟器验收软件闭环，保留能够复现上述问题的回归用例。
6. R07：完成 ESP-IDF 驱动和板卡验证，再验收 TASK-017 及物理终端功能。

当前可以确认的是部分独立模块测试通过；系统组装、终端协议和物理驱动仍有阻塞，不能由现有测试数量推导整体完成率。
