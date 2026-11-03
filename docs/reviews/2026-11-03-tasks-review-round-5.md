# TASK-001～020 第五轮复审

- 日期：2026-11-03。
- 范围：`b017d4b..ce88249`，对照第四轮 R4-01～R4-05，重点检查产品固件新增运行路径。
- 结论：**Request changes：剩余 1 个 P1、3 个 P2。** 联网初始化、真实 JSON 解析、动作序列化、文本/指标绘制、原生电容触摸与默认禁用门禁均已接入，不再沿用上一轮“这些功能完全未实现”的判断。
- 本轮仅新增审查报告，未修改实现或测试，未提交；未连接 ESP32、访问串口或刷机，未修改已有硬件调查文件。

## 上轮修复核对

| 上轮编号 | 当前状态 | 依据 |
|---|---|---|
| R4-01 联网初始化 | 初始化缺口关闭；生产 TLS 仍缺配置 | 已创建 Wi-Fi、MQTT 并注册回调，取得 IP 后启动客户端；详见 R5-01 |
| R4-02 下行报文 | 固定示例与缺失指标处理已修；分片未修 | cJSON 已解析告警、指标和状态，使用真实字段；详见 R5-02 |
| R4-03 上行动作 | 裸 UUID 与开机时长问题已修；未校时路径仍有缺口 | 已序列化完整 JSON 并改用 time()，但异步 SNTP 未完成时仍发送；详见 R5-04 |
| R4-04 屏幕渲染 | 仅背景色问题关闭；新增缓冲区生命周期错误 | 已绘制标题、摘要、展示倒计时、高优先级手动提示及三行指标；详见 R5-03 |
| R4-05 原生触摸 | 源码缺口关闭，实机可靠性交互仍待验收 | 使用 TOUCH_PAD_NUM5 原生电容采样；SPRITE_ENABLE_TOUCH_ACTIONS 默认关闭，禁用时不触发交互 |

## 待修问题

### R5-01 [P1] MQTT TLS 没有配置信任来源，无法连接生产监听器

- 位置：[main.c:456](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:456)。关联 R4-01、TASK-017。
- `mqtt_cfg` 仅设置 URI、身份、遗嘱与重连等参数，未设置 `broker.verification.certificate`、`crt_bundle_attach` 或已加载的全局 CA；也没有其他路径给客户端配置可信 CA。
- 项目 Mosquitto 配置只在回环地址开放 1883，对外使用 TLS 8883。即使把可配置 URI 改为 `mqtts://实际主机:8883`，当前 ESP-TLS 配置仍会返回 `ESP_ERR_MBEDTLS_SSL_SETUP_FAILED`，因此补齐 Wi-Fi 与 MQTT 初始化尚不足以连接项目的生产服务。
- 核对本地 ESP-IDF `components/esp-tls/esp_tls_mbedtls.c:672–718`：没有任何验证来源且未允许跳过验证时，明确报 `No server verification option set` 并失败。即使 sdkconfig 启用了证书 bundle，也需要给客户端显式附加。
- 修复：配置实际 Broker 证书链所需的可信 CA 或证书 bundle，并验证主机名及有效期；不要通过关闭服务端验证绕过。生产凭据继续留在本地配置中。

### R5-02 [P2] 把每个 MQTT 数据片段当成完整 JSON，合法长告警丢失

- 位置：[main.c:404](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:404)。关联 R4-02、TASK-015/017。
- 回调仅复制 `event->data_len` 后立即解析，没有使用 `total_data_len`、`current_data_offset` 或缓存首片 topic。本地 ESP-MQTT 默认接收缓冲区为 1024 字节，当前客户端没有覆盖该大小；2048 字节的局部数组不会扩大底层接收缓冲区。
- ESP-MQTT 对较长报文多次触发 DATA 事件，后续片段的 topic 为 NULL、topic_len 为 0。首片 JSON 不完整，解析失败；后续片段无法匹配 `/down/alert`，整条消息被丢弃。
- 已用实际 `AlertPayload` Schema 构造合法 **1167 字节**消息，包含 24 个中文标题字符、48 个中文摘要字符及 512 字符 URL，低于 Broker 的 2048 字节上限。在宿主探针中原样抽取本次 parser 和 MQTT 回调，链接实际 cJSON 与共享调度器：完整输入 `active=1`；按 960 + 207 字节、正确 offset 及后续空 topic 输入，结果 `active=0`。
- 修复：按报文总长度有界重组，保留首片 topic，仅在收到全部数据后解析；拒绝超长或不连续片段，并在断线时清理。补充该长度范围的回归验证。

### R5-03 [P2] 异步 LCD 发送尚未完成就释放或覆盖像素缓冲区

- 位置：[st7789.c:141](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/hal/st7789.c:141)，同文件 `fill_rect()` 第 120～130 行。
- `draw_glyph()` 将栈上的 `uint16_t buf[64]` 交给 `esp_lcd_panel_draw_bitmap()` 后立即返回；下一字形会复用栈。`fill_rect()` 则在下一次绘制前重填同一静态缓冲区。这些内存都可能仍被上一笔 DMA 读取，导致字形或矩形像素错误。
- ESP-IDF `esp_lcd_panel_io.h:85–89` 明确规定颜色数据在后台传输，必须等 `on_color_trans_done()` 才能回收；SPI 实现直接保存颜色指针。下一次绘制命令内部的等待发生在调用方已经改写缓冲区之后，不能保证安全。
- 临时异步 panel 桩保留上一笔指针，运行原样抽取的实际 `fill_rect()`，连续绘制两种颜色，确认上一笔缓冲区在下一次 draw 调用前已被覆盖。该探针验证所有权错误，不代表已在真实屏幕观察到乱码。
- 修复：让像素缓冲区存活且保持不变直至发送完成，使用完成通知配合同步等待，或正确管理多缓冲区的使用权；覆盖字形和矩形两条路径。

### R5-04 [P2] 未完成 SNTP 同步仍上报动作，云端判过期后弹窗已关闭

- 位置：[main.c:500](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:500)。关联 R4-03、TASK-016/017。
- 此问题在显式启用触摸交互后触发。取得 IP 时 SNTP 与 MQTT 随即启动，SNTP 是异步的；NTP 尚未成功或不可达时，MQTT 仍可能已在线并收到告警。
- `unix_now < 1700000000` 分支只打印警告，随后仍把未校准的时间写入动作。共享上报器生成 `expire_at = timestamp + 10`，实际云端 `ActionRouter` 将其判为 `action_expired`。设备随后无条件消除告警，用户点击后不会打开目标页面。
- 修复：以明确的校时成功状态约束协议动作发送；未同步时保留告警并提供可重试路径，避免发送无效动作。补充“MQTT 已连接、SNTP 未同步”的回归用例。默认触摸门禁应继续保持关闭，直到既定实机验收完成。

## 验证结果与边界

| 验证 | 结果 |
|---|---|
| Python `tests/` 全套 | **82 passed，4 warnings，15.14s** |
| 共享 C 指标、调度、手势测试 | **10 个测试函数通过**，`cc -Wall -Wextra -std=c99`，临时目录编译运行 |
| ESP-IDF 默认配置完整构建 | **退出码 0**，956 步；镜像 0xf04a0，app 分区剩余 6% |
| ESP-IDF 启用触摸分支构建 | **退出码 0**；镜像 0xf0ee0，app 分区剩余 6% |
| 合法长告警回调探针 | 同一 1167 字节报文：完整输入入队，分片输入丢失 |
| 矩形异步缓冲区探针 | 原样函数在下一次 draw 前覆盖仍待消费的上一笔像素 |
| TLS 与校时失败路径 | 静态核对产品代码、当前 SDK 和云端路由；未连接生产 Broker 或实板运行 |
| 真实屏幕、音频、触摸阈值与设备闭环 | **未执行实机验收**，不以构建成功或宿主探针替代 |

Python 命令：

```sh
uv run --no-project --with pytest --with fastapi --with 'pydantic>=2' --with httpx --with paho-mqtt python -m pytest tests -q
```

固件两次构建均在 `firmware/` 的临时副本完成：加载本机 ESP-IDF 环境后先执行 `idf.py build`，随后仅在副本中启用 `CONFIG_SPRITE_ENABLE_TOUCH_ACTIONS=y` 再次构建。所有 C 探针和构建临时目录均已清理，仓库 sdkconfig、构建产物与板卡均未被改动。
