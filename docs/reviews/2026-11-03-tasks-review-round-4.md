# TASK-001～020 第四轮复审

- 日期：2026-11-03。
- 范围：修复提交 `8313c62..b017d4b`，对照第三轮 R3-01～R3-07，并读取当前板型规范。
- 结论：**Request changes。上一轮 7 项关闭 6 项；R3-02 部分修复，剩余 5 处确定的产品固件代码缺口（3 个 P1、2 个 P2）。**
- Python 82 项、共享 C 10 个测试函数通过，ESP-IDF 完整构建成功。构建成功不能证明产品入口已经连通网络或实现实际界面。
- 本轮只新增此报告，未修改实现和测试，未操作 ESP32、串口或刷机，未改动已有硬件调查文件。

## 第三轮问题处理结果

| 编号 | 结果 | 核对依据 |
|---|---|---|
| R3-01 Webhook 离线静默成功 | 关闭 | 接口检查连接与 publish 返回值，失败返回 503 / 50301 / retryable=true；实际未启动 MQTTService 的签名请求得到 503 |
| R3-02 物理固件无运行链路 | 部分修复 | 新增触摸/UI FreeRTOS 任务、ES8311 I2C/I2S 与提示音路径，TASK-017 已标未验收；联网初始化、协议处理、实际 UI 和原生触摸仍缺失，详见下文 |
| R3-03 模拟器主屏手势只有日志 | 关闭原实现缺口 | 已使用共享手势模块，更新数值/百分比状态、30 分钟静音截止时间，音频条件和状态栏均使用该状态；本轮未执行完整 SDL 鼠标交互验收 |
| R3-04 strict 模式认证失败 exit=0 | 关闭 | 等待 CONNACK 并检查拒绝/超时；隔离认证 Broker 实测错误密码 exit=1，正确密码 exit=0 |
| R3-05 中文逐字节变问号 | 关闭 | UTF-8 解码、常用中文字形、缺字方框和换行已接入；实际 SDL 软件渲染探针确认中文不同于六个问号，¥/天字形存在，第二行有绘制 |
| R3-06 正向 E2E 未运行模拟器 | 关闭原测试替身问题 | 现已启动真实 C 模拟器进程，经真实 Broker 接收状态和告警，断言调度、UI 事件及音频日志；coding/meeting/sleep 用例通过。headless 日志断言不等于 SDL 像素或物理终端验收 |
| R3-07 将 TTL 当弹窗倒计时 | 关闭 | medium 读取调度器 remaining_sec；high 显示手动确认提示；共享调度器测试通过 |

## R3-02 剩余代码问题

以下均能从代码确定，不需要通过刷机才发现。TASK-017 应区分“软件尚未实现”与“软件完成但待实机验收”。

### R4-01 [P1] 产品入口没有初始化 Wi-Fi/MQTT，也没有注册回调

- 位置：[main.c:239](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:239)。对应 TASK-017。
- `app_main()` 初始化 NVS、HAL 和共享状态后只创建两个任务；整个产品固件中没有 `esp_netif_init`、`esp_wifi_init`、`esp_mqtt_client_init` 或事件回调注册。`s_mqtt_client` 始终为 NULL，新增的 Wi-Fi/MQTT handler 没有调用方。
- 因此当前镜像即使成功烧录，也不会连接网络、订阅告警或上报动作；这不是单纯缺少实机验证。
- 修复要求：接入可配置的 Wi-Fi/Broker 参数，创建并启动网络与 MQTT 客户端、注册回调和订阅，落实约定的认证/TLS 与重连。凭据通过本地配置提供，不写入仓库。

### R4-02 [P1] 下行告警被替换为固定示例，指标消息没有处理

- 位置：[main.c:85](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:85)。对应 TASK-014/015/017、SPEC-002。
- `/down/alert` 分支不解析报文，所有告警都被改为 `PRIORITY_MEDIUM`、10 秒、标题 `New Notification`、ID `evt_hw`。真实 high 会被降级并自动消失，标题/摘要/音效/交互关联/有效期均丢失。即使补齐 R4-01，这些错误仍然存在。
- `/down/metrics` 完全没有处理分支，`s_has_metrics` 始终为 false，`s_last_metrics` 从未接收数据，云端额度无法进入看板模型。
- 修复要求：按 SPEC-002 有界解析完整下行结构并填充真实字段，接入指标消息；处理 ESP-MQTT 的 `data_len` 和分片，避免当前 state 分支直接对不保证 NUL 结尾的 `event->data` 调用 `strstr`。

### R4-03 [P1] 触摸上行只发布 UUID，时间也使用了开机时长

- 位置：[main.c:132](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:132)。对应 TASK-016/017、SPEC-002 动作契约。
- `action_reporter_build_action()` 虽然构建了动作对象，publish 的 payload 却只有 `act.action_id`。云端要求包含 device_id、created_at、expire_at 和 data.active_alert_id 的完整 JSON；裸 UUID 必然解析失败。将同形式 payload 交给实际 ActionRouter，结果为 `malformed_payload`。
- 构建动作时传入的 `now_sec` 来自 `esp_timer_get_time()`，是开机秒数；共享上报器直接将其写入 created_at/expire_at。即使只补 JSON 序列化，云端仍会按 Unix 时间将动作判为过期。
- 修复要求：调用已有 `action_reporter_serialize_json()` 并发布完整报文，检查构建/序列化/发布结果；协议时间使用已同步的 Unix 秒，手势和展示时长保留单调时钟。携带真实下行 event_id，完成设备动作到 host command 的关联验证。

### R4-04 [P2] 物理渲染任务仍然只填充整屏颜色

- 位置：[main.c:198](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:198)。对应 TASK-013/014/017、SPEC-003。
- `ui_and_alert_task()` 的显示输出只有告警背景色与主屏背景色两次 `bsp_st7789_fill()` 调用；没有标题、摘要、三行指标或倒计时的绘制，也没有将计算出的 `s_metrics_model` 输出到屏幕。修复网络和解析后，用户依然只能看到颜色，无法读取通知或额度。
- 修复要求：把共享状态与布局接到真实 ST7789 绘制路径，渲染通知内容和指标，medium 使用展示剩余时间、high 使用手动确认提示；用合法中文、缺字回退及 240×240 边界验证实际输出。

### R4-05 [P2] GPIO5 原生电容电极仍按 TTP223 数字电平读取

- 位置：[ttp223.c:8](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/hal/ttp223.c:8)、[ttp223.h:7](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/hal/ttp223.h:7)。对应 TASK-016/017、SPEC-005/007。
- 本次将引脚改为 GPIO5，但驱动仍配置普通 GPIO 输入和下拉，并用 `gpio_get_level()==1` 判断触摸。选定板型的 TP_KEY 是直连 GPIO5 的原生电容电极，没有 TTP223 输出数字高低电平。
- 当前 SPEC-005 第 3.0 节、SPEC-007 与 board-profile 明确记录这一区别；此前三次触摸没有数字电平变化，原生电容采样的可靠按下/松开关联也尚未通过。因此仅修改 pin 和注释不能提供单击/长按输入。
- 修复要求：按 GPIO5 / TOUCH_PAD_NUM5 接入原生电容测量、基线与阈值，再将可靠按下状态传入共享手势模块；可靠交互验收前，按现有板型规范禁用产品入口的触摸行为。不要把电容阈值校准未通过写成已完成的 TTP223 输入。

## 本轮验证

| 验证 | 结果与边界 |
|---|---|
| Python `tests/` 全套 | **82 passed，4 warnings，16.00s**；包含真实 Broker 与 C 模拟器进程的正向链路 |
| 共享 C 指标/调度/手势测试 | **10 个测试函数通过**：2 + 4 + 4，使用临时目录产物 |
| ESP-IDF 完整构建 | **退出码 0，956 步成功**；镜像大小 0x50a00，app 分区 0x100000，剩余 69% |
| SDL2 + libmosquitto 模拟器 | 本轮从当前源码在临时目录编译成功 |
| 真实离线 MQTTService + 合法签名 Webhook | **HTTP 503 / code=50301 / retryable=true** |
| 隔离认证 Broker + 实际模拟器 strict 模式 | **错误密码 exit=1；正确密码 exit=0** |
| 实际 SDL 软件字体渲染 | 中文与六个问号的像素不同；¥/天存在字形；自动换行第二行有输出 |
| 裸 UUID 上行 + 实际 ActionRouter | **malformed_payload**，验证 R4-03 的云端拒绝行为 |
| 板卡、串口、刷机、完整鼠标交互和生产 TLS/ACL | **本轮未执行**，不以构建或 headless 日志替代这些验收 |

Python 命令：

```sh
uv run --no-project --with pytest --with fastapi --with 'pydantic>=2' --with httpx --with paho-mqtt python -m pytest tests -q
```

ESP-IDF 验证将 `firmware/` 复制到临时目录（排除 build、模拟器二进制与 Python 缓存），加载 `/Users/liruoxuan/esp/esp-idf/export.sh` 后在副本执行 `idf.py build`。未修改仓库 sdkconfig/build，也未连接板卡。Broker、模拟器和字体探针均已结束，临时目录已清理。

建议按联网初始化 → 下行协议与时钟 → 实际屏幕输出 → 上行动作闭环的顺序补齐软件；原生触摸在既有硬件验证任务下完成可靠性验收后再启用。
