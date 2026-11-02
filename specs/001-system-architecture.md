# SPEC-001: Desk Sprite 系统架构总览与拓扑设计

- **版本**: 1.4.0 (深度闭环版)
- **状态**: 待复审
- **更新日期**: 2026-11-02
- **关联规范**: [SPEC-002 通信协议](002-protocol-and-events.md), [SPEC-003 视觉与状态机](003-sprite-display-and-state-machine.md), [SPEC-004 Mac客户端](004-mac-cli-and-daemon.md)

---

## 1. 系统定位与演进阶段 (Phase Boundary)

Desk Sprite 是一个基于 **ESP32-S3 硬件终端**、**腾讯云主脑服务** 与 **Mac 本地工作站** 协同的物理事件中枢与桌面伴侣。

为了确保开发稳步落地，项目遵循严谨的阶段演进模型：

### 1.1 Phase 1 (MVP 首版收敛范围)
- **部署拓扑**: 1 台 Mac 宿主 (`macbook_pro`) 与 1 台 ESP32-S3 终端 (`esp32_c049ef`) 静态一对一绑定。
- **核心闭环 1 (通知与视听联动)**: Mac 端通过 `sprite notify` 或 GitHub Webhook 下发通知 -> 云端存储 EventRecord -> 终端两阶段过滤与排队 -> 240×240 IPS 弹窗 + ES8311 音频播报。
- **核心闭环 2 (双向物理闭环与归属校验)**: 用户触摸终端 TTP223 按键 -> 上报 `active_alert_id` -> 云端严格校验事件归属与可交互性 -> 派发带确定性哈希 `command_id` 的指令 -> Mac 守护进程通过原子去重在默认浏览器中安全打开对应 URL -> 上报 ACK 回执。
- **核心闭环 3 (命令执行包装器与管道同步屏障)**: `sprite run -- <cmd>` 通过管道同步屏障（Pipe-based Sync Barrier）与 TTY 控制权交接（`tcsetpgrp`）支持交互命令（不被 SIGTTIN 挂起），保持命令退出码与信号语义，超时 3.0s 通知保护。
- **核心闭环 4 (度量看板与本地 Stale 判定)**: 终端常驻渲染三行额度看板（支持模拟与固定接入，支持基于 `sampled_at` 和 `ttl_sec` 的本地断网灰显）。
- **核心闭环 5 (基础状态感知)**: 原生广播监听锁屏瞬时上报（<500ms），前台应用经 15 秒防抖映射为 `coding`/`meeting`，支持 30 分钟手动覆盖。

### 1.2 Phase 2 (后续扩展范围)
- 第三方 LLM 账单复杂动态爬虫与自动化 Session 维护。
- IMAP 邮件长连接监听与 LLM 智能长文提炼。
- 基于云端绝对时间戳的多轮番茄钟与健康疲劳关怀。
- 多设备、多主机动态绑定。

---

## 2. 总体拓扑与三层架构

系统采用 **“瘦物理边缘端，强云端大脑，协同本地工作站 (Thin Edge, Fat Cloud, Local Bridge)”** 架构：

```mermaid
flowchart TD
    subgraph LocalMac["Mac 本地工作站 (Local Workstation)"]
        MacCLI["sprite CLI (一键通知 / 管道同步屏障包装器)"]
        MacDaemon["sprite-daemon (锁屏广播监听 / URL 原生安全唤起 / LRU 原子去重)"]
    end

    subgraph ExternalSources["外部事件源"]
        GH["GitHub (PR 状态 / Workflow CI Webhook, HMAC 签名验签)"]
    end

    subgraph CloudBrain["腾讯云服务端 (Cloud Hub & Broker)"]
        FastAPI["FastAPI 核心网关 (Webhooks / HTTP API, API Key 授权校验 / 409 幂等处理)"]
        MQTTBroker["MQTT Broker (Mosquitto, TLS + 严格 Topic ACL)"]
        MetricsEngine["度量看板管理模块 (采样时间与 Stale 维护)"]
        EventStore["云端事件状态存储与归属校验 (24h 有界淘汰)"]
        DeduplicationCache["确定性哈希命令派发 (10m TTL 缓存)"]
        
        FastAPI --> EventStore
        FastAPI --> MQTTBroker
        MetricsEngine --> MQTTBroker
        EventStore <--> DeduplicationCache
    end

    subgraph ESP32["ESP32-S3 小电视终端 (Physical Edge)"]
        Client["MQTT 瘦客户端 (ESP-IDF v5.1.2, LWT 遗嘱)"]
        LVGL["LVGL v8.3.11 渲染管线 (240×240 ST7789V / SDL2 模拟器)"]
        Audio["ES8311 I2S 音频解码器 (WAV 播报, PA_EN 硬件静音)"]
        Touch["TTP223 单点电容按键 (单击 / 长按手势状态机)"]
        
        Client --> LVGL
        Client --> Audio
        Touch --> Client
    end

    %% 正向消息链路
    ExternalSources -->|HMAC 验签 Webhook| FastAPI
    MacCLI -->|Bearer Token HTTP POST (403 越权防护)| FastAPI
    FastAPI -->|内部派发| MQTTBroker
    MacDaemon -->|sprite/host/{id}/up/state| MQTTBroker
    MQTTBroker == "sprite/device/{id}/down/# (QoS 0/1)" ==> Client

    %% 反向动作链路
    Client -.->|"sprite/device/{id}/up/action (UUIDv4 action_id, active_alert_id, 10s 有效期)"| MQTTBroker
    MQTTBroker -.->|"Topic 提取 ID -> EventStore 归属校验 -> 派发 command"| EventStore
    EventStore -.->|"sprite/host/{id}/down/command (确定性 command_id, expire_at)"| MacDaemon
    MacDaemon -.->|"原生系统调用 (open, 结构化 URL 校验, 禁 shell)"| LocalMac
    MacDaemon -.->|"sprite/host/{id}/up/ack (执行回执: success / duplicate_ignored)"| MQTTBroker
```

---

## 3. 分层职责与工程边界

### 3.1 物理终端：ESP32-S3 (瘦边缘端)
- **基准硬件方案**: ESP32-S3-DevKitC-1-N8R8 + ST7789V 1.54寸 240×240 IPS + ES8311 Codec (含 PA_EN) + TTP223 单点电容触摸按键。
- **软件栈**: ESP-IDF v5.1.2，LVGL v8.3.11（支持 SDL2 PC 模拟仿真开发）。
- **职责**:
  - 维护与云端 MQTT 的安全长连接心跳（Keepalive 30s，LWT 离线遗嘱）。
  - 双行缓冲渲染桌宠动画（128×128 像素 Spritesheet，8 FPS）与三行看板。
  - **两阶段模式过滤与调度**: 入队时根据当前 Base Mode 拦截（如锁屏丢弃 medium）；出队时以最新模式复核（排队切锁屏清空，切会议静音）；调度时检查 `expire_at` 淘汰陈旧通知。
  - 解析 TTP223 电容按键输入（单击、长按 ≥1200ms），生成带标准 UUIDv4 与 10 秒有效期的动作上报。

### 3.2 云端主脑：Tencent Cloud Runtime (中心控制与数据聚合)
- **软件栈**: Python 3.11+ / FastAPI / Mosquitto MQTT Broker。
- **职责**:
  - 提供安全 HTTP API 与 GitHub Webhook 接收端（强制 HMAC 验签、409 幂等冲突拦截与 403 越权防护）。
  - 严格依据 Broker ACL 保护的 Topic 路径提取可信实体 ID，防范跨设备身份伪造。
  - **维护事件记录存储（EventStore）**: 保存下发事件与目标设备、宿主、URL 映射（24h 有界 TTL）；动作上报时强制校验归属，安全拒绝跨设备冒名与陈旧操作。
  - 维护终端动作与 Mac 命令的确定性映射（`command_id = "cmd_" + sha256(action_id)[:16]`），10 分钟缓存去重，杜绝重复派发。

### 3.3 本地工作站：Mac CLI & Daemon (工作流中枢)
- **职责**:
  - **`sprite notify`**: 快速向小电视发送视听通知。
  - **`sprite run`**: 在 TTY 环境下通过**管道同步屏障（Pipe-based Sync Barrier）**与 `tcsetpgrp` 转移控制权，无竞态支持交互命令（不被 SIGTTIN 挂起）；非 TTY 环境继承 FD 0/1/2 零损透传；保持原退出码；3.0s 通知超时保护。
  - **`sprite-daemon`**:
    - 基于 macOS 原生广播监听锁屏（<500ms 瞬时上报 `sleep`），周期轮询探测活跃应用（15s 防抖），支持 30 分钟手动锁定。
    - 接收 `open_browser` 指令，原子登记 in-flight 杜绝并发双开，校验过期时间与结构化 URL 安全白名单，上报执行回执。
    - 提供 macOS LaunchAgent 动态绝对路径开机自启安装。

---

## 4. 异常处理与恢复策略 (Fault Recovery)

1. **Mac 掉线与状态退避**:
   - Mac 守护进程配置 MQTT 遗嘱消息（LWT）：`sprite/host/{host_id}/up/status` -> `{"status": "offline"}`。
   - 当云端检测到 Mac 离线超过 60 秒，自动向绑定的 ESP32 下发 `work_mode: "idle"`。
2. **ESP32 断网重连与本地降级**:
   - ESP32 掉线时状态栏指示灯转为黄色，启动指数退避重连；
   - 掉线超 1 小时，终端本地自动将三行指标灰显标示为 `[STALE]`。
3. **过期与重复指令拦截**:
   - 终端物理按键上报自带 10 秒有效期，云端超时直接丢弃。
   - 云端下发的宿主操作带 `expire_at`（60s），Mac 采用 `clean_session = true` 干净会话，进程重启后超时消息直接丢弃；并发命令原子拦截并返回 `duplicate_ignored`。
   - High 告警即使在硬件展示永不自动关闭，云端事件记录在 24 小时后安全淘汰，防止无限期悬空带来的幽灵调用。
