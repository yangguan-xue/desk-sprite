# TASK-001～020 第六轮复审

- 日期：2026-11-03。
- 范围：`ce88249..2bcddf0`，复核第五轮 R5-01～R5-04 及新增回归测试。
- 结论：**Request changes，剩余 1 个 P2。** R5-01～03 的原缺口关闭；R5-04 已阻止未校时动作，但误用了读取后重置的 SNTP 状态，连续操作仍有错误。
- 本轮只新增报告，未修改实现和测试、未提交；未连接板卡、操作串口或刷机，保留已有工作区与硬件调查文件。

## 上轮处理结果

| 编号 | 结果 | 核对依据 |
|---|---|---|
| R5-01 TLS 信任配置 | 关闭原配置缺口 | 支持自定义 CA 或显式附加 ESP x509 bundle，保持主机名验证；实际 Broker 证书与握手仍待部署验收 |
| R5-02 MQTT 分片 | 关闭 | 接入共享有界重组器，校验长度与连续 offset、保留首片 topic，完整后解析，断线重置；1167 字节及异常分片用例通过 |
| R5-03 LCD 缓冲生命周期 | 关闭正常发送路径的原缺口 | 注册完成回调，通过信号量等待后返回，矩形和字形均使用同步包装；实际函数的延迟完成桩验证 10 次发送均在返回前完成 |
| R5-04 未同步动作 | 部分修复 | 未同步及离线时保留告警，合法时间生成完整 JSON；新增门禁使用了会消费完成状态的 SDK API，见 R6-01 |

## R6-01 [P2] 将一次性 SNTP 完成状态当成持续有效的校时标记

- 位置：[main.c:346](/Users/liruoxuan/work/workspace/programs/desk-sprite/firmware/main/main.c:346)，调用位置第 559 行。关联 R5-04、TASK-016/017。
- 在显式启用触摸交互后，每次点击告警都会调用 `is_time_synced()`，它直接判断 `esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED`。
- 当前 SDK 中该 API 是 `sntp_get_sync_status()` 的别名。`COMPLETED` 读取后会立即重置为 `RESET`，并不表示设备时钟随后失效。第一次点击消耗完成状态，下一条告警再点击时，即使系统时间仍正确且 MQTT 在线，也会被 `TOUCH_ALERT_ACT_BLOCKED_UNSYNCED` 拦截。离线状态下点击同样可能先消耗完成状态，再导致重连后的重试被阻止。
- 当前 `CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`，若不再产生新的同步完成事件，后续点击会持续被阻止；正常情况下要到下一次定时校时才再次放行一次。
- SDK 证据：`components/lwip/include/apps/esp_sntp.h:116–126` 明确说明读取完成状态后会重置；`components/lwip/apps/sntp/sntp.c:82–100` 实现了该重置。
- 已复现：在宿主探针中原样抽取当前 `is_time_synced()` 与本地 SDK 的状态读取函数，初始化一次 `COMPLETED`，链接实际 `touch_action`、动作序列化和告警调度模块，连续投递并点击两条 high 告警。系统 Unix 时间有效且始终在线，第一次 `synced=1 / result=0`，第二次 `synced=0 / result=1 / retained=1`。其中 result=0 为正常动作，result=1 为未同步拦截。
- 修复要求：在 SNTP 成功通知中记录持续有效的“已校时”状态，或消费 `COMPLETED` 后缓存该状态，并结合实际时钟有效性判断。补充一次同步后连续两条告警，以及离线点击后恢复连接再重试的用例。继续保持可靠性交互验收前默认禁用触摸的门禁。
- 现有 `test_touch_action_sntp_sync_guard()` 直接传入手工设置的布尔值，只验证门禁下游逻辑，无法发现生产入口生成该布尔值时的错误。

## 验证

| 验证 | 结果与边界 |
|---|---|
| Python 全套 | **82 passed，4 warnings，17.35s** |
| C 测试 | **5 个可执行文件、16 个测试函数通过**：指标 2、调度 4、手势/动作 5、分片 3、LCD 模型 2；`cc -Wall -Wextra -std=c99` |
| LCD 实际函数补充探针 | 原样抽取完成回调、bitmap 包装、矩形和字形函数；延迟完成桩确认 **10 次发送均等待完成后返回** |
| 连续告警校时门禁探针 | **复现 R6-01**；使用实际门禁函数、SDK 状态读取实现及共享动作/调度代码 |
| ESP-IDF 默认配置完整构建 | **退出码 0，958 步**；镜像 0xf5190，app 分区 0x100000，剩余约 4% |
| ESP-IDF 启用触摸分支构建 | **退出码 0**；镜像 0xf5cd0，剩余约 4% |
| Git whitespace 检查 | `git diff --check ce88249..2bcddf0` 通过 |
| 实机及部署 | 未验证真实 Broker TLS、屏幕像素、音频、触摸阈值与设备闭环；构建和宿主测试不替代这些验收 |

LCD 新增的两个仓库测试使用独立 mock 模型，没有编译生产 HAL，因此本轮额外抽取实际函数验证正常完成路径；错误/超时恢复未作实机验证。

Python 命令：

```sh
uv run --no-project --with pytest --with fastapi --with 'pydantic>=2' --with httpx --with paho-mqtt python -m pytest tests -q
```

两次固件构建均复制 `firmware/` 到临时目录，排除 build、模拟器二进制与缓存，加载 `/Users/liruoxuan/esp/esp-idf/export.sh` 后执行 `idf.py build`。第二次仅在副本 sdkconfig 中启用触摸。临时构建目录和 C 探针均已清理，未修改仓库 sdkconfig 或构建产物。
