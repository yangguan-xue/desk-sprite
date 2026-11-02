# SPEC-001: Desk Sprite 系统架构与通信契约

## 1. 架构总览

采用 **“瘦物理边缘端，强云端大脑 (Thin Edge, Fat Cloud)”** 架构：

```mermaid
flowchart TD
    subgraph Producers["事件源 (Producers)"]
        MacCodex["Mac: Codex / 终端任务钩子"] -->|HTTP / CLI| CloudBrain
        KimiCron["云端定时任务: Kimi / LLM 账本"] -->|内部轮询| CloudBrain
        ExternalWebhooks["GitHub CI / GPU 训练任务"] -->|Webhook| CloudBrain
    end

    subgraph TencentCloud["腾讯云服务器 (主运行时)"]
        CloudBrain["Hub 核心服务 (FastAPI)"]
        Broker["MQTT Broker (Mosquitto)"]
        CloudBrain -->|内部推送| Broker
    end

    Broker -- "MQTT 长连接 (毫秒级下发)" --> ESP32

    subgraph ESP32Device["小电视 (物理渲染与播报)"]
        ESP32["ESP32-S3 (瘦客户端)"]
        ESP32 --> Screen["240×240 IPS 动态桌宠 + 仪表盘"]
        ESP32 --> Audio["ES8311 扬声器 (提示音/播报)"]
        Touch["触摸按键"] -.->|"上报: 已读/交互"| Broker
    end
```

---

## 2. 通信协议定义 (JSON Envelope)

### 2.1 常规指标更新 (`metrics`)
- **Topic**: `sprite/device/metrics`
- **Payload**:
```json
{
  "type": "metrics",
  "data": {
    "bars": [
      {"label": "Kimi 5H", "value": 0, "max": 100},
      {"label": "Kimi Week", "value": 0, "max": 100},
      {"label": "Kimi Total", "value": 83, "max": 100}
    ],
    "pet_mood": "idle"
  }
}
```

### 2.2 事件弹窗与播报 (`alert`)
- **Topic**: `sprite/device/alert`
- **Payload**:
```json
{
  "type": "alert",
  "data": {
    "title": "Codex 任务完成",
    "sub": "耗时 3m12s · 花费 1.2k tokens",
    "sound": "ding",
    "pet_mood": "cheer",
    "duration_sec": 10
  }
}
```

### 2.3 物理交互上报 (`touch_event`)
- **Topic**: `sprite/server/event`
- **Payload**:
```json
{
  "type": "touch_event",
  "action": "tap",
  "timestamp": 1793614000
}
```
