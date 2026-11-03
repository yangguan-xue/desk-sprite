# TASK-001～020 第三轮复审

- 日期：2026-11-03。
- 范围：修复提交 `12c63ba..8313c62`，对照第二轮 RR01～RR09，并检查当前工作区产品固件入口。
- 结论：**Request changes，仍不能按全部任务完成验收。上一轮 9 项中 3 项关闭、5 项部分修复、1 项未修复；本轮保留 6 项，并发现新增倒计时问题，共 2 个 P1、5 个 P2。**
- 本轮仅新增审查报告。未修改产品代码或测试，未操作 ESP32、串口或刷机；保留工作区已有硬件调查与开发改动。

## 第二轮问题处理结果

| 编号 | 结果 | 核对依据 |
|---|---|---|
| RR01 不可投递仍成功、首次连接失败不重连 | 部分修复 | notify 已检查连接与 publish 返回值，失败返回可重试错误；默认连接开启，Broker 晚启动实测恢复。Webhook 仍忽略投递失败，见 R3-01 |
| RR02 固定种子 UUID | 关闭 | ESP 使用 esp_fill_random，宿主使用系统随机源；真实 C 上报器跨进程及云端路由回归通过 |
| RR03 物理固件入口仅颜色测试 | 未修复 | 当前产品入口仍轮播纯色，音频驱动仍缺 I2C/I2S 与播放链路，见 R3-02 |
| RR04 按下立即当单击、长按打开 URL | 部分修复 | 已等待释放并区分长短按，弹窗长按静默关闭；主屏数值切换与临时静音仍仅打印日志，见 R3-03 |
| RR05 模拟器身份、凭据与 TLS 配置缺失 | 部分修复 | 已派生 dev_{device_id} 并支持密码、CA、TLS；实际认证拒绝仍被 strict 模式判为成功，见 R3-04 |
| RR06 明文监听绑定全网卡 | 关闭配置问题 | 1883 已绑定 127.0.0.1；本轮未执行生产 TLS 与外部网络部署验证 |
| RR07 remaining 比例取反 | 关闭 | current/total 语义统一，18/30 显示 60%，实际 C 测试通过 |
| RR08 无文字与 Mock 标记 | 部分修复 | 已接 ASCII 字体、文本、Mock 字段；中文和非 ASCII 单位仍变问号，见 R3-05；新倒计时语义错误见 R3-07 |
| RR09 E2E 替身与 ACK 被吞 | 部分修复 | 反向使用真实 C 动作输出、DaemonMQTTWorker 并断言 up/ack；正向仍未启动模拟器，见 R3-06 |

## 仍需修复的问题

### R3-01 [P1] Webhook 仍在 MQTT 离线时返回成功

- 位置：[webhook.py:108](/Users/liruoxuan/work/workspace/programs/desk-sprite/cloud/app/api/v1/webhook.py:108)。对应 RR01、TASK-004。
- 新 publish() 会在离线时返回 False，但此接口完全忽略返回值，仍返回 HTTP 200 / code=0。EventStore 存储不是待投递队列，也没有补发路径，因此该次 CI/PR 通知丢失，HTTP 响应无法提示调用方重试。
- 实测：向未启动 MQTT 的真实应用提交 HMAC 签名合法、action=completed、conclusion=success 的 workflow_run，得到 `200 success`，同时 `mqtt_connected=False`，publish 日志明确为 message rejected。
- 修复要求：Webhook 同样处理不可投递状态，返回可重试错误，或落入真正会补发且遵守 TTL 的队列。notify 的修复不能覆盖此入口。

### R3-02 [P1] 产品固件仍没有物理通知交互运行链路

- 位置：[main.c:30](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:30)、[es8311.c:8](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/hal/es8311.c:8)。对应 RR03、TASK-017。
- app_main 初始化外设后，每 1.5 秒轮播纯色，没有运行 Wi-Fi/MQTT、实际通知 UI、触摸轮询与调度。ES8311 仍只配置功放 GPIO，音量函数只有日志，没有 codec I2C、I2S 与播放路径。
- 屏幕 SPI、背光 PWM、输入 GPIO 基础已经存在；板级调查和独立诊断工具也有进展，但不能据此验收产品入口的视听交互闭环。本次修复提交没有改变这些缺口。
- 修复要求：按已核实板型完成产品入口与驱动集成；完成前将 TASK-017 保留为未验收。本轮只做源码审查，没有执行 ESP-IDF 构建或硬件验证。

### R3-03 [P2] 主屏点击与长按仍没有实际功能

- 位置：[main.c:303](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:303)、[main.c:313](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:313)。对应 RR04、TASK-016、SPEC-003 第 6 节。
- 主屏单击分支只输出 toggle unit display，长按只输出 temporary mute 30 min。没有切换展示状态，也没有静音截止时间；播放路径仍只检查告警自带 force_mute，所以用户长按后后续 medium 仍响铃，单击也不会切换百分比。
- SDL 手势还另写了 <500ms / ≥1500ms 判定，没有复用已测试的共享手势模块，与契约 50～300ms / ≥1200ms 不同。
- 修复要求：真正更新数值/百分比状态、30 分钟临时静音状态与音频条件，并从 SDL 输入验证。弹窗长按不上报 URL 的原危险行为已修复，不再列为残留。

### R3-04 [P2] strict MQTT 模式把认证失败当成成功退出

- 位置：[sim_mqtt.c:239](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/sim_mqtt.c:239)、[main.c:203](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:203)。对应 RR05、TASK-013。
- mosquitto_connect 成功只代表建立传输连接，认证结果由后续 CONNACK 回调返回。init 此时直接返回 0；main 的 strict_mqtt 只检查同步返回值，既不等待认证成功，也不把回调拒绝转为失败。
- 实测：临时启动启用 password_file、禁止匿名的隔离 Broker，以 dev_esp32_c049ef 和错误密码运行实际编译的 `--headless --once --strict-mqtt`，日志为 `Broker rejected connection: rc=5`，退出码仍为 0。相同条件使用正确密码，日志确认 Connected，退出码同样为 0。
- 修复要求：strict 模式在有界超时内等待认证成功，并将拒绝/超时返回非零。打印 Subscribed 也不能替代认证与订阅确认。凭据和 TLS 参数的配置能力已实现，本项不再声称它们缺失。

### R3-05 [P2] 新字体把合法中文通知和指标单位全部画成问号

- 位置：[main.c:22](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:22)、[main.c:41](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:41)。对应 RR08、TASK-013/014。
- sim_draw_string 按字节遍历 UTF-8，sim_draw_char 将所有 >127 字节替换为 '?'。因此真实中文 title/summary 无法阅读；默认指标中的 `¥`、`天` 同样损坏。ASCII 文本确实已经接入，不能再概括为完全没有文字。
- 实测：在临时 C 探针中包含实际 main.c，使用 SDL 软件 renderer 调用实际 sim_draw_string；“中文”和“??????”的像素缓冲逐字节完全一致。
- 修复要求：使用支持 UTF-8 解码与所需中文字形的渲染路径。当前字符串函数还不换行且遇换行立即终止，后续应一并用合法中文通知验证摘要展示和 240×240 边界。

### R3-06 [P2] 正向 E2E 仍没有验证实际终端输出

- 位置：[test_notification_flow.py:109](/Users/liruoxuan/work/workspace/programs/desk-sprite/tests/e2e/test_notification_flow.py:109)、[test_notification_flow.py:206](/Users/liruoxuan/work/workspace/programs/desk-sprite/tests/e2e/test_notification_flow.py:206)。对应 RR09、TASK-018/020。
- 本次将 Python 重写的过滤规则换成了实际 C alert_filter，属于有效改进；但设备端仍是 Python MQTT subscriber，随后把测试指定的 mode/priority 作为命令行参数传给孤立 C 过滤器。screen_wake 仍由 decision==2 推导。
- 测试没有启动 sim_mqtt/main、完整队列、SDL 或音频路径，模式也没有通过 MQTT 进入模拟器。因此“正常弹窗响铃”和模式切换的测试通过，仍不能证明实际终端行为，也无法发现本轮主屏静音、字体、倒计时和认证错误。
- 修复要求：运行实际模拟器，让状态和告警经过其 MQTT 入口，并断言实际 UI/音频事件及关键模式切换。保留当前过滤器用例作为模块集成测试。反向 daemon 与 ACK 的原问题已修复。

### R3-07 [P2] 新增倒计时显示消息有效期，和弹窗实际剩余时长不一致

- 位置：[main.c:373](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/sim/main.c:373)。本次新增问题，对应 SPEC-003 第 3 节、TASK-013/015。
- UI 对所有优先级显示 `expire_at - now`，但 expire_at 是消息有效期，medium 展示由调度器 remaining_sec / duration_sec 决定。正常发送 duration_sec=10、expire_at=now+300 的通知，窗口先显示约 300 秒，却在约 10 秒后消失；排队后的差异也会继续存在。
- High 必须手动消除，规范明确无倒计时，当前却同样显示 Expires，并最终停在 0 秒，制造即将自动关闭的错误提示。
- 修复要求：medium 从调度器的展示剩余时间渲染；High 显示手动确认提示，避免将消息 TTL 当作 UI 倒计时。

## 本轮验证

| 验证 | 结果 | 范围 |
|---|---|---|
| Python `tests/` 全套 | **79 passed，4 warnings** | 当前现有用例通过，E2E 边界见 R3-06 |
| C 指标、调度、手势三组测试 | **10 个测试函数通过** | 实际共享模块，未操作硬件 |
| SDL2 + libmosquitto 模拟器 | **编译成功，无编译诊断** | 临时目录产物，未覆盖仓库二进制 |
| 隔离认证 Broker + 实际模拟器 | 正确密码连接；错误密码被 rc=5 拒绝但 exit=0 | 复现 strict 模式误报，不代表生产 TLS/ACL 已验收 |
| 云端首次连接失败后启动 Broker | **自动恢复 connected=True** | 关闭 RR01 的首次连接失败恢复问题 |
| 真实签名 Webhook + 离线 MQTT | **200 success、未连接、消息被拒绝** | 复现 R3-01 |
| SDL 实际字体函数软件渲染 | **中文与六个问号像素一致** | 复现 R3-05，无窗口或硬件依赖 |
| ESP-IDF 构建、板卡与生产 TLS/ACL | 本轮未执行 | 不把其他任务的硬件诊断当作本轮产品验收 |

Python 验证命令：

```sh
uv run --no-project --with pytest --with fastapi --with 'pydantic>=2' --with httpx --with paho-mqtt python -m pytest tests -q
```

所有 C 验证与 Broker 探针均使用临时目录和本机隔离端口，进程已结束。建议先补 Webhook 投递失败处理和真实模拟器正向集成测试，再依测试修正交互及 UI；TASK-017 继续按实测板型完成。
