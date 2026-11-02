# SPEC-002: 通信协议与数据事件契约 (MQTT / API)

- **版本**: 1.4.0 (深度闭环版)
- **状态**: 待复审
- **更新日期**: 2026-11-02
- **关联规范**: [SPEC-001 系统架构](001-system-architecture.md), [SPEC-003 视觉与状态机](003-sprite-display-and-state-machine.md), [SPEC-004 Mac客户端](004-mac-cli-and-daemon.md)

---

## 1. 通信架构、身份标识与 Topic 命名空间

系统通过 MQTT 3.1.1 协议进行双向通信。Payload 统一使用 UTF-8 编码的 JSON 字符串，单包最大长度限制为 **2048 字节**。

### 1.1 客户端身份命名规范与 ACL 权限

为了避免字符串拼接时前缀重复，全链路统一遵循以下命名规范：
- **`host_id`**: 纯字符串标识符，**不带** `host_` 前缀（示例：`"macbook_pro"`）。
- **`device_id`**: 纯字符串标识符，**不带** `dev_` 前缀（示例：`"esp32_c049ef"`）。

| 实体角色 | 逻辑 ID 示例 | MQTT Client ID (`client_id`) | MQTT 认证用户名 (`username`) | 凭证存储位置 | Mosquitto ACL 限制 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **云端引擎** | `cloud` | `cid_cloud_engine` | `cloud_admin` | 服务端安全环境变量 | 全 Topic 读写 (Superuser) |
| **Mac 宿主** | `macbook_pro` | `cid_host_macbook_pro` | `host_macbook_pro` | `~/.sprite/config.json` | 仅读 `sprite/host/macbook_pro/down/#`<br/>仅写 `sprite/host/macbook_pro/up/#` |
| **ESP32 终端** | `esp32_c049ef` | `cid_dev_esp32_c049ef` | `dev_esp32_c049ef` | 固件 NVS 分区 | 仅读 `sprite/device/esp32_c049ef/down/#`<br/>仅写 `sprite/device/esp32_c049ef/up/#` |

### 1.2 MQTT 3.1.1 可信身份判定机制
由于 MQTT 3.1.1 的 PUBLISH 消息不携带发布者用户名，系统建立 **Broker ACL 隔离与 Topic 信任转移机制**：
1. **Broker 强制隔离**: Mosquitto 开启严格 ACL 鉴权，客户端 `host_X` 只能发布至 `sprite/host/X/up/#`；客户端 `dev_Y` 只能发布至 `sprite/device/Y/up/#`。任何跨路径发布被 Broker 直接拒绝。
2. **云端受信原则**: 云端以订阅到的 **Topic 路径中的 ID 作为唯一合法的实体标识**。收到消息后，云端校验 Payload 中的 `device_id` 或 `host_id` 必须与 Topic 提取的 ID 完全一致，不一致则直接丢弃并告警。

### 1.3 Topic 路由表

| Topic 路径 | 发布者 | 订阅者 | QoS | Retain | 业务语义 |
| :--- | :--- | :--- | :---: | :---: | :--- |
| `sprite/device/{device_id}/down/metrics` | 云端 | 终端 | 0 | True | 周期度量看板数据下发 |
| `sprite/device/{device_id}/down/alert` | 云端 | 终端 | 1 | False | 即时通知与弹窗事件下发 |
| `sprite/device/{device_id}/down/state` | 云端 | 终端 | 1 | True | 工作状态机下发 |
| `sprite/device/{device_id}/up/action` | 终端 | 云端 | 1 | False | 终端物理触摸/按键动作上报 |
| `sprite/device/{device_id}/up/status` | 终端 | 云端 | 1 | True | 终端在线生命周期状态 (含 LWT) |
| `sprite/host/{host_id}/up/state` | Mac | 云端 | 1 | True | Mac 工作状态主动同步上报 |
| `sprite/host/{host_id}/up/status` | Mac | 云端 | 1 | True | Mac 在线生命周期状态 (含 LWT) |
| `sprite/host/{host_id}/down/command` | 云端 | Mac | 1 | False | 云端向指定 Mac 下发原生系统操作 |
| `sprite/host/{host_id}/up/ack` | Mac | 云端 | 1 | False | Mac 执行系统操作的结果回执 |

---

## 2. 全局枚举与类型系统 (唯一基准定义)

- **通知优先级 (`priority`)**: `low`, `medium`, `high`。
- **音频标识 (`sound`)**: `none`, `ding`, `chime`, `cheer`, `sigh`, `alert_beep`。
- **桌宠动作 (`pet_action`)**: `idle`, `coding`, `meeting`, `cheer`, `sigh`, `alert`, `sleep`。
- **工作模式 (`work_mode`)**: `idle`, `coding`, `meeting`, `sleep`。

---

## 3. MQTT 核心 Payload 契约与 Schema 约束 (H03 闭环)

所有消息顶层必须包含 `version: 1`。
**未知版本处理规则**: 若接收端收到 `version > 1` 的消息，必须对已知字段进行尽力解析，未知字段安全忽略，**严禁崩溃退出**。

### 3.1 仪表盘度量下发 (`sprite/device/{device_id}/down/metrics`)
```json
{
  "type": "metrics",
  "version": 1,
  "timestamp": 1793614800,
  "data": {
    "is_mock": false,
    "bars": [
      {
        "id": "kimi_token",
        "label": "Kimi 5H",
        "current": 42000,
        "limit": 100000,
        "unit": "tok",
        "percent": 42,
        "direction": "used",
        "sampled_at": 1793614740,
        "ttl_sec": 3600,
        "status": "normal"
      },
      {
        "id": "deepseek_cost",
        "label": "DeepSeek月度",
        "current": 28.5,
        "limit": 100.0,
        "unit": "¥",
        "percent": 28,
        "direction": "used",
        "sampled_at": 1793614700,
        "ttl_sec": 3600,
        "status": "normal"
      },
      {
        "id": "copilot_sub",
        "label": "Copilot订阅",
        "current": 18,
        "limit": 30,
        "unit": "天",
        "percent": 60,
        "direction": "remaining",
        "sampled_at": 1793611100,
        "ttl_sec": 3600,
        "status": "stale"
      }
    ],
    "pet_mood_hint": "idle"
  }
}
```
- **字段约束**:
  - `bars`: 必填数组，1 ~ 3 个元素。
  - `bars[].label`: 必填字符串，1 ~ 12 字符（UTF-8）。
  - `bars[].current` / `limit`: 必填数值，`limit > 0`。
  - `bars[].unit`: 必填字符串，1 ~ 4 字符。
  - `bars[].percent`: 必填整数，0 ~ 100。
  - `bars[].direction`: 必填枚举 `"used" | "remaining"`。
  - `bars[].sampled_at`: 必填秒级时间戳。
  - `bars[].ttl_sec`: 必填整数（默认 3600）。
  - `bars[].status`: 必填枚举 `"normal" | "warning" | "stale" | "error"`。

### 3.2 即时通知下发 (`sprite/device/{device_id}/down/alert`)
```json
{
  "type": "alert",
  "version": 1,
  "event_id": "evt_gh_20261102_001",
  "timestamp": 1793614820,
  "expire_at": 1793615120,
  "data": {
    "category": "github_pr",
    "priority": "medium",
    "title": "PR #42 构建通过",
    "summary": "耗时 3m12s · 已自动合并",
    "source": "github.com/my-org/repo",
    "sound": "ding",
    "pet_action": "cheer",
    "duration_sec": 8,
    "interactive": {
      "tap_action": "open_url",
      "target_url": "https://github.com/my-org/repo/pull/42",
      "target_host": "macbook_pro"
    }
  }
}
```
- **字段约束**:
  - `event_id`: 必填字符串，最大 64 字节，全局唯一。
  - `expire_at`: 必填整数，绝对秒级时间戳（默认 `timestamp + 300`）。
  - `title`: 必填字符串，1 ~ 24 字符。
  - `summary`: 必填字符串，1 ~ 48 字符。
  - `source`: 可选字符串，最大 32 字符。
  - `priority`: 必填枚举 (`low` | `medium` | `high`)。
  - `sound`: 必填枚举 (`none` | `ding` | `chime` | `cheer` | `sigh` | `alert_beep`)。
  - `pet_action`: 必填枚举 (`idle` | `coding` | `meeting` | `cheer` | `sigh` | `alert` | `sleep`)。
  - `duration_sec`: 整数，3 ~ 60 秒（默认 8）。若 `priority == "high"` 时忽略此字段。
  - `interactive`: 可选对象。若存在，`target_url` 必须满足结构化校验，长度 ≤ 512 字节。

### 3.3 工作状态同步 (`down/state` 与 `up/state`)
- **Mac 上报 (`sprite/host/{host_id}/up/state`)**:
  - 必填字段: `type="host_state_update"`, `version=1`, `host_id`, `timestamp`, `data.work_mode`。
- **云端下发 (`sprite/device/{device_id}/down/state`)**:
  - 必填字段: `type="state_sync"`, `version=1`, `timestamp`, `data.work_mode`。

### 3.4 终端物理动作上报 (`sprite/device/{device_id}/up/action`)
```json
{
  "type": "device_action",
  "version": 1,
  "action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
  "device_id": "esp32_c049ef",
  "created_at": 1793614860,
  "expire_at": 1793614870,
  "data": {
    "action": "tap",
    "active_alert_id": "evt_gh_20261102_001",
    "current_mode": "coding"
  }
}
```
- **字段约束**:
  - `action_id`: 必填，标准 36 字符 UUIDv4。
  - `device_id`: 必填字符串，必须与 MQTT Topic 中的 ID 完全一致。
  - `expire_at`: 必填，严格为 `created_at + 10`。
  - `data.action`: 必填枚举 `"tap" | "double_tap" | "long_press"`。
  - `data.active_alert_id`: 字符串或 null（主屏时为 null）。

### 3.5 Mac 宿主操作下发 (`sprite/host/{host_id}/down/command`)
$$\text{command\_id} = \text{"cmd\_"} + \text{sha256}(\text{action\_id})[:16]$$
针对示例 `action_id = "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b"`，其计算结果严格为 `cmd_7540dd3e9fcadef4`。

```json
{
  "type": "host_command",
  "version": 1,
  "command_id": "cmd_7540dd3e9fcadef4",
  "source_action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
  "created_at": 1793614861,
  "expire_at": 1793614921,
  "action": "open_browser",
  "params": {
    "url": "https://github.com/my-org/repo/pull/42"
  }
}
```
- **字段约束**:
  - `command_id`: 必填字符串，最大 32 字符。
  - `expire_at`: 必填整数，默认 `created_at + 60`。
  - `action`: 固定为 `"open_browser"`。
  - `params.url`: 必填合规 URL，最大 512 字节。

### 3.6 Mac 执行结果回执 (`sprite/host/{host_id}/up/ack`)
```json
{
  "type": "command_ack",
  "version": 1,
  "host_id": "macbook_pro",
  "command_id": "cmd_7540dd3e9fcadef4",
  "status": "success",
  "error_code": 0,
  "message": "URL opened successfully",
  "timestamp": 1793614862
}
```
- `status`: 必填枚举 `"success" | "duplicate_ignored" | "expired" | "rejected" | "failed"`。

### 3.7 在线生命周期状态 (`sprite/.../up/status`)
- **LWT 遗嘱消息 (Retain=True)**: `{"type": "status_lifecycle", "version": 1, "status": "offline", "reason": "connection_lost", "timestamp": 1793614000}`
- **上线通知 (Retain=True)**: `{"type": "status_lifecycle", "version": 1, "status": "online", "client_version": "1.4.0", "timestamp": 1793614005}`
- **优雅退出 (Retain=True)**: `{"type": "status_lifecycle", "version": 1, "status": "offline", "reason": "graceful_shutdown", "timestamp": 1793618000}`

---

## 4. 云端事件存储、归属校验与动作路由流水线 (H01 闭环)

终端在 `device_action` 中仅上报 `active_alert_id`，绝不上报 URL，因此云端必须建立严密的**事件状态存储（Event Record Store）**与归属校验流水线：

```mermaid
flowchart TD
    RecvNotify[云端收到 notify API 或 Webhook] --> GenEventID[生成唯一 event_id]
    GenEventID --> StoreRecord[保存至云端 EventStore<br/>- target_device: esp32_c049ef<br/>- target_host: macbook_pro<br/>- target_url: https://...<br/>- is_interactive: true<br/>- record_ttl: 24h<br/>- state: pending]
    StoreRecord --> PushAlert[下发 sprite/device/{id}/down/alert]
    
    SubAction[云端收到 sprite/device/{src_dev}/up/action] --> CheckTopic{Topic 提取的 src_dev<br/>与 Payload device_id 一致?}
    CheckTopic -- 不一致 (伪造) --> DropAlert[直接丢弃并告警]
    CheckTopic -- 一致 --> CheckNull{active_alert_id<br/>是否为空?}
    CheckNull -- 为空 (主屏操作) --> ProcessMainScreen[执行主屏业务, 不生成命令]
    CheckNull -- 非空 --> LookupStore[在 EventStore 检索 active_alert_id]
    
    LookupStore --> Found{记录是否存在?}
    Found -- 不存在 / 已淘汰 --> DismissOnly[仅通知设备关闭弹窗<br/>不生成 host_command]
    Found -- 存在 --> VerifyOwner{记录中的 target_device<br/>== src_dev ?}
    VerifyOwner -- 否 (跨设备冒名) --> SecurityReject[安全拦截并记录审计日志]
    VerifyOwner -- 是 --> CheckInteractive{is_interactive == true<br/>且 URL 非空?}
    
    CheckInteractive -- 否 (纯文本告警) --> MarkConsumed1[标记 state: consumed<br/>消除弹窗, 不生成命令]
    CheckInteractive -- 是 --> Deduplicate[哈希生成 command_id<br/>检查 10m 去重缓存]
    Deduplicate --> DispatchHost[下发 sprite/host/{target_host}/down/command]
```

### 4.1 事件记录存储契约 (EventStore)
- **存储字段**:
  - `event_id`: 唯一标识符。
  - `target_device`: 授权接收该告警的设备 ID。
  - `target_host`: 绑定的目标 Mac 宿主 ID。
  - `target_url`: 安全校验通过的跳转 URL（无交互时为 null）。
  - `is_interactive`: 布尔值（是否有可交互 URL）。
  - `created_at`: 创建时间戳。
  - `state`: 枚举 `"pending" | "consumed" | "expired"`。
- **保留期与 High 长期展示淘汰策略**:
  - 普通 Medium 告警记录在内存中保留 10 分钟。
  - High 告警即使在硬件屏幕上长期展示（无自动超时），云端 EventStore **保留上限严格设为 24 小时（24h TTL）**。
  - 若用户在 24 小时后才按下硬件消除按键：云端因记录已淘汰判定为未知事件，仅通知终端关闭弹窗，**安全拒绝派发陈旧命令**，杜绝历史状态永久悬空引发的幽灵网页唤起。

---

## 5. HTTP API 与幂等契约 (FastAPI) (H03 闭环)

基准路径：`/api/v1`。接口标准返回体：`{"code": 0, "message": "success", "data": {}}`。

### 5.1 HTTP 状态码与分类定义表
| HTTP 状态码 | 业务 Code | 触发场景与原因 | 返回数据结构 |
| :--- | :---: | :--- | :--- |
| **200 OK** | `0` | 请求成功并被接收派发 | `{"code": 0, "message": "success", "data": {"event_id": "..."}}` |
| **400 Bad Request** | `40001` | 请求非合法 JSON、必填字段缺失、字符超长 | `{"code": 40001, "message": "Validation error: title exceeds 24 chars", "data": null}` |
| **401 Unauthorized**| `40101` | Bearer Token 缺失、格式无效或 Webhook HMAC 验签失败 | `{"code": 40101, "message": "Invalid API token or signature", "data": null}` |
| **403 Forbidden** | `40301` | **越权防护**: 指定的 `target_device` 不在当前 Key 的 `allowed_devices` 中 | `{"code": 40301, "message": "Forbidden: Device not authorized", "data": null}` |
| **404 Not Found** | `40401` | 指定的 `target_device` 或 `host_id` 在系统中不存在 | `{"code": 40401, "message": "Device not found", "data": null}` |
| **409 Conflict** | `40901` | **幂等冲突**: 相同 Idempotency-Key 重复提交但请求体不一致 | `{"code": 40901, "message": "Idempotency key conflict: payload mismatch", "data": null}` |
| **422 Unprocessable**| `42201`| 枚举值非法（如 `priority="info"`）、URL 包含分号/非法字符 | `{"code": 42201, "message": "Invalid enum or malformed URL", "data": null}` |

### 5.2 发送通用通知 (`POST /api/v1/notify`) 与幂等键契约
- **Headers**:
  - `Authorization: Bearer <SPRITE_API_KEY>`
  - `Idempotency-Key: <uuid>` (可选)
- **Request Body**:
```json
{
  "target_device": "esp32_c049ef",
  "priority": "medium",
  "title": "测试通过",
  "summary": "全部 42 个用例运行完毕",
  "source": "pytest",
  "sound": "ding",
  "pet_action": "cheer",
  "duration_sec": 8,
  "url": "https://ci.example.com/reports/42"
}
```
- **`Idempotency-Key` 严格处理规则 (H03 闭环)**:
  1. **调用者命名空间隔离**: 键格式为 `owner_id + ":" + idempotency_key`，不同 API Key 的 Key 互不影响。
  2. **保存周期**: 云端在 Redis/内存中缓存 5 分钟（300 秒）。
  3. **相同 Key + 相同 Body**: 直接返回缓存的首次响应体与首次生成的 `event_id`，**绝不重复生成事件与下发 MQTT**。
  4. **相同 Key + 不同 Body**: 判定为客户端使用冲突，严格返回 **HTTP 409 Conflict (`code: 40901`)**。

---

## 6. 验收标准与验证命令 (Verification Matrix) (H05 闭环)

| 需求项 | 验证入口与命令 | 预期结果 |
| :--- | :--- | :--- |
| **事件归属校验** | 模拟设备 A (`esp32_A`) 上报属于设备 B 的 `active_alert_id` | 云端拦截并记录审计日志，绝不向 Mac 派发指令 |
| **无 URL 通知点击处理** | 下发无 URL 的普通纯文本通知，模拟终端上报 tap | 云端识别 `is_interactive == false`，仅消除弹窗，不生成 `host_command` |
| **HTTP 幂等冲突拦截** | 使用同一 `Idempotency-Key` 先后发送两个不同 title 的请求 | 第 1 次返回 200，第 2 次返回 HTTP 409 Conflict (`code: 40901`) |
| **未知协议版本兼容** | 构造 `version: 2` 但包含合法已知字段的 MQTT 消息 | 各端正常处理已知字段，未知字段安全忽略，进程不崩溃 |
| **Broker ACL 隔离验证 (H05 闭环)** | 客户端以 `dev_esp32_c049ef` 账号向 `sprite/device/esp32_other/up/action` 发布消息 | 订阅探针收不到消息，云端无指令下发，Mosquitto 日志记录 `Access Denied` |
