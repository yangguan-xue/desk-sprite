# TASK-001～020 第二轮复审

- 日期：2026-11-03。
- 范围：第一轮报告 R01～R13；审查 `a3c9214..12c63ba` 的修复提交，并读取当前工作区涉及固件的改动。
- 结论：**Request changes，仍不能按全部任务完成验收。原 13 项中 7 项可关闭，6 项仍有缺口；本轮列出 9 个具体问题：3 个 P1、6 个 P2。**
- 第一轮报告保留。本轮仅新增此报告；已有硬件调查、规格、工具与固件改动不属于本轮交付。未操作串口或刷写设备。

## 第一轮问题的处理结果

| 原编号 | 结果 | 核对依据 |
|---|---|---|
| R01 默认公开测试凭证 | 关闭 | 默认鉴权注册表为空，测试凭证需显式注入；缺失 Webhook 密钥时拒绝验签 |
| R02 跨宿主越权 | 关闭 | 增加 allowed_hosts，并在 notify 鉴权阶段校验 target_host |
| R03 云端 MQTT 运行链路 | 部分修复 | 已组装服务与 lifespan；离线时仍静默丢弃并返回成功，见 RR01 |
| R04 daemon 入口 API 错误 | 关闭 | 组件 API 与锁屏回调已接通，真实入口启动、SIGTERM 测试通过 |
| R05 动作协议不兼容 | 部分修复 | JSON、时间字段、UUID 长度及 active_alert_id 已对齐；UUID 跨进程重复，见 RR02 |
| R06 模拟器绕过调度器 | 关闭原问题 | 已接入共享过滤、队列和 tick；手势接入另有缺口，见 RR04 |
| R07 物理固件实现缺失 | 未关闭 | 已有屏幕、PWM 和 GPIO 基础；产品运行链路与音频驱动仍缺失，见 RR03 |
| R08 TLS 未实现 | 部分修复 | 云端、daemon 和 Broker 增加 TLS；明文监听及模拟器接入仍有问题，见 RR05、RR06 |
| R09 指标未解析和渲染 | 部分修复 | 已解析 bars 与逐行时间，进度条可随数据变化；比例及可读 UI 未完成，见 RR07、RR08 |
| R10 存储未清理 | 关闭 | EventStore、幂等缓存均增加写入清理及 5000 条容量上限 |
| R11 非 TTY 后代进程遗留 | 关闭 | 建立子进程组并使用 killpg，新增后代退出测试 |
| R12 未完成 CI 被当失败 | 关闭 | 过滤未完成事件，区分失败、取消与跳过 |
| R13 E2E 未验证实际系统 | 部分修复 | 已使用真实 Broker，但终端与 daemon 仍被测试替身代替，ACK 路径实际报错，见 RR09 |

## 必须修复的问题

### RR01 [P1] MQTT 不可用时通知仍返回成功，消息不会补发

- 位置：[mqtt_service.py:121](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/app/services/mqtt_service.py:121)、[main.py:34](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/app/main.py:34)、[notify.py:145](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/app/api/v1/notify.py:145)。对应原 R03、TASK-004～006。
- `publish()` 在未连接时只 append 调试列表；该列表没有重放路径。默认 lifespan 不连接 MQTT，notify 仍返回 HTTP 200 / code=0，并缓存幂等成功响应。即使显式启用 MQTT，连接尚未建立或断线期间也会触发相同行为，调用者重试相同幂等键无法恢复通知。
- 运行复现：向使用默认 MQTT 配置、显式注入测试鉴权的应用发合法 notify，返回 `200 success`，同时 `mqtt_connected=False`、客户端尚未创建。
- 启动恢复也未闭合：[mqtt_service.py:105](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/app/services/mqtt_service.py:105) 的同步 connect 失败会跳过 loop_start。探针先在空闲端口启动服务，再启动隔离 Broker，服务仍无网络线程（`_thread=None`），不会自行连接。
- 修复要求：明确生产连接配置；不可投递时返回可重试错误，或进入真正会补发且遵守 TTL 的有界队列。启动首次失败必须进入重连流程，成功响应与幂等缓存要与投递语义一致。

### RR02 [P1] 固定随机种子使重启后的动作被误判为重复

- 位置：[action_reporter.c:6](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/src/engine/action_reporter.c:6)，云端去重见 [action_router.py:119](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/app/services/action_router.py:119)。对应原 R05、TASK-016/019。
- UUID 使用固定初值 `0x5a17e001` 的伪随机序列，每次设备或模拟器重启都会重复整套 ID，多台设备的序列也一致。云端根据 action_id 派生命令 ID，并在全局缓存中去重 10 分钟。
- 运行复现：分别执行两次实际 C 上报器，首个 ID 均为 `fecbbb19-9c99-4276-bb44-6e826f5ee5a8`。分别绑定两个不同交互事件交给真实 ActionRouter，结果依次为 `command_dispatched`、`duplicate_skipped`。
- 修复要求：使用平台随机源产生 UUID，保证跨重启和跨设备唯一；增加跨进程生成及云端路由回归用例。仅格式符合 UUIDv4 不足以保证唯一性。

### RR03 [P1] 物理固件入口仍仅运行屏幕颜色测试

- 位置：[main.c:30](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:30)、[es8311.c:8](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/hal/es8311.c:8)。对应原 R07、TASK-017。
- 当前入口初始化外设后，每 1.5 秒轮播纯色，没有 Wi-Fi、ESP-MQTT、实际 UI、手势轮询和消息调度。ES8311 初始化仅配置功放使能 GPIO，音量函数仍仅输出日志，缺少 codec I2C 初始化、I2S 和音频播放路径。
- 屏幕 SPI、背光 PWM、输入 GPIO 已比第一轮有实质进展，但这些基础无法组成通知视听交互闭环。README 也明确仓库产品固件尚需按新板级调查结果适配；独立诊断工具和设备现有 Kimi 看板不能替代该入口的实现。
- 修复要求：接通产品任务与真实驱动，并按实测板型适配；完成前应保留 TASK-017 未验收。本轮未进行 ESP-IDF 编译或硬件操作，不将已有 build 目录或其他任务的诊断记录当作本轮验收结果。

### RR04 [P2] 模拟器把所有按下立即当成单击，长按也会打开 URL

- 位置：[main.c:210](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:210)。对应 TASK-013/016、SPEC-003 第 6 节。
- SDL_MOUSEBUTTONDOWN 立即生成 GESTURE_TAP 并消除弹窗，没有处理释放、按住时长，也没有调用共享 touch_button 状态机。用户原本想长按静默关闭交互弹窗，刚按下就已上报打开 URL；主屏单击切换数值/百分比、长按临时静音 30 分钟也没有执行路径。
- 修复要求：将按下、持续和释放接入共享手势判定，按当前界面状态执行对应行为，并从模拟器输入验证长按不发布打开 URL 动作。

### RR05 [P2] 模拟器身份与项目 Broker ACL 不匹配，无法实际联调

- 位置：[main.c:136](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:136)、[acl.conf:16](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/deploy/mosquitto/acl.conf:16)。对应 TASK-001/013、原 R08。
- 模拟器硬编码用户名 `sprite_device`；项目 ACL 授权的是 `dev_esp32_c049ef`，TASK-013 也明确要求 `dev_{device_id}`。当前仅能配置 host/port，没有用户名、密码或 CA 配置，也没有 TLS 设置。因此即使把 Broker 账号密码配齐，当前身份也拿不到设备 Topic 权限，8883 同样无法完成 TLS 连接。
- 主入口还忽略 sim_mqtt_init 的失败返回值。本轮 smoke 中实际打印 `Connect failed: Bad file descriptor`，进程仍返回 0，不能据此声称 MQTT 验收成功。现有 E2E 的匿名 Broker 绕开了身份问题。
- 修复要求：按设备 ID 派生身份，从运行配置注入密码和 TLS 参数，正确处理初始化失败；以项目认证和 ACL 配置验证模拟器收发。

### RR06 [P2] 声称仅供内部使用的明文监听绑定全部网卡

- 位置：[mosquitto.conf:4](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/deploy/mosquitto/mosquitto.conf:4)。对应原 R08、TASK-001。
- 新增 8883 不会限制 1883。明文监听仍为 `listener 1883 0.0.0.0`，同样使用用户名密码认证；在外部网络能够访问该端口时，凭证和消息仍可走明文。注释“本地监听器”没有形成网络隔离约束。
- 修复要求：生产关闭明文监听，或绑定实际受控的内部接口并提供部署隔离配置；完成正确 CA 可连接、错误证书被拒绝及外部无法访问明文端口的验证。

### RR07 [P2] remaining 指标被再次取反，剩余额度显示错误

- 位置：[ui_metrics.c:34](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/src/ui/ui_metrics.c:34)，实际上游数据见 [metrics_engine.py:53](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/app/services/metrics_engine.py:53)。对应原 R09、TASK-014。
- 云端 Copilot 指标为 current=18、limit=30、percent=60、direction=remaining，表示剩余 18 天。终端却按 `(total-current)/total` 得到 40%，与上游的 60% 和“剩余比例”契约不一致。
- 运行复现：将该组值输入实际 ui_metrics_compute_render_model，progress_ratio=0.40。现有 C 单测将 remaining 的 current 当成已用值，因此通过测试仍掩盖了跨端语义冲突。
- 修复要求：统一 current、percent 与 direction 的含义，按实际云端报文验证剩余 18/30 显示 60%。

### RR08 [P2] 窗口只画矩形，通知和指标文字没有显示路径

- 位置：[main.c:255](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:255)、[main.c:279](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:279)。对应原 R09、TASK-013/014。
- 弹窗绘制仅有色块，三行看板仅有轨道和进度条。绘制路径没有使用告警 title/summary、rendered_text、show_mock_badge，也没有文本或字形渲染，用户无法读到通知内容、额度数字、标签、[M]、[STALE] 或倒计时。控制台日志无法替代窗口 UI。
- 此外 metrics 解析器从零初始化结构后没有读取 data.is_mock，即使接通角标渲染也会丢失云端的 Mock 标志。
- 修复要求：接通实际可读的 UI 渲染，并将 Mock 标志传入各指标行；用真实消息验收通知文本、数值和状态标记。

### RR09 [P2] E2E 仍未启动 C 终端及真实 daemon，ACK 异常被吞掉

- 位置：[test_notification_flow.py:109](/Users/liruoxuan/work/workspace/programs/desk-sprite/tests/e2e/test_notification_flow.py:109)、[test_notification_flow.py:175](/Users/liruoxuan/work/workspace/programs/desk-sprite/tests/e2e/test_notification_flow.py:175)、[test_action_browser_flow.py:110](/Users/liruoxuan/work/workspace/programs/desk-sprite/tests/e2e/test_action_browser_flow.py:110)。对应原 R13、TASK-018～020。
- 正向测试现在确实经过真实 Broker，但设备端仍是 Python subscriber，弹窗、亮屏和静音结论来自测试内重新编写的 evaluate_terminal_mode_response，而非 C 调度器或模拟器实际输出。反向动作也是 Python 手写报文，宿主为临时回调，没有使用 daemon 的 MQTTWorker。这些测试仍无法发现 RR02、RR04、RR05、RR08。
- 回执路径还存在确定错误：executor.execute 返回 CommandAckPayload，直接 json.dumps(ack) 会抛 TypeError，被 except Exception: pass 吞掉；回执 Topic 也写成 up/status 而非 up/ack。测试只断言 URL 已打开和记录已消费，没有确认 ACK，所以仍然通过。
- 运行复现：调用真实 CommandExecutor，再执行测试相同序列化，得到 `Object of type CommandAckPayload is not JSON serializable`。
- 修复要求：启动实际 C 模拟器与 daemon 消息组件，保留浏览器打开函数的可控替身；断言真实 UI/音频事件、动作报文及 ACK 到达，后台异常必须使测试失败。

## 验证结果与边界

| 验证 | 本轮结果 | 能证明的范围 |
|---|---|---|
| Python tests/ | 77 passed，5 warnings | 现有单测及真实 Broker 网络用例通过；不能替代缺失的终端/daemon E2E |
| C 指标、调度、手势三组测试 | 10 个测试函数通过 | 共享模块单测，不等于全部集成行为正确 |
| make -C firmware/sim test_build | 编译及 headless smoke 返回 0 | 本机已链接 SDL2、libmosquitto；运行时连接失败，不能算 Broker 联调通过 |
| C 上报器跨进程 + ActionRouter 探针 | 复现重复 UUID 和 duplicate_skipped | 实际 C 输出送入实际云端校验与路由 |
| 默认 FastAPI 通知探针 | 200 success，MQTT 未连接且客户端未创建 | 复现无投递成功响应 |
| Broker 晚启动探针 | Broker 可连接后，云端网络线程仍为 None | 复现首次 connect 失败后未启动重连 |
| 实际 C remaining 渲染探针 | 18/30 渲染为 0.40 | 复现跨端比例语义错误 |
| ACK 序列化探针 | TypeError | 复现测试吞掉的回执异常 |
| ESP-IDF 构建、板卡及生产 TLS 联调 | 本轮未执行 | 不覆盖其他任务已有硬件诊断，也不据此否认已有外设基础进展 |

Python 测试使用：

```sh
uv run --no-project --with pytest --with fastapi --with 'pydantic>=2' --with httpx --with paho-mqtt python -m pytest tests -q
```

C 测试与探针均在临时目录生成可执行文件，头文件使用 `-I firmware/src/include`。未改产品代码或测试文件，未清理其他任务的工作区内容。

建议先处理 RR01、RR02，完成认证 Broker 与实际模拟器联调，再用 RR09 的真实端点测试验证 RR04、RR07、RR08；物理固件按实测板型继续完成 RR03。
