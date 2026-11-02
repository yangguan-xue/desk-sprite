# Desk Sprite (桌宠小精灵) 👾

> 一个基于 ESP32-S3 小电视物理终端与腾讯云主运行时的**赛博桌面 AI 伴侣与物理事件中枢**。

---

## 🌟 核心特性
- **云端主脑 (Tencent Cloud Runtime)**：7×24h 永续在线，自动聚合 Kimi / LLM Token 消耗，接收各类任务完成通知。
- **物理像素桌宠 (ESP32-S3)**：240×240 IPS 高亮屏幕，动态情绪状态机（闲置/工作中/庆祝/告警）。
- **物理声音反馈 (ES8311 I2S)**：任务完成清脆“叮！”声播报，摆脱屏幕切窗焦虑。
- **极简开发者工作流**：支持命令行钩子一键推送（如 `codex-run && sprite notify "任务搞定"`）。

---

## 📁 目录结构
```text
desk-sprite/
├── specs/       # 系统架构规范与 MQTT 通讯契约
├── cloud/       # 腾讯云后端服务 (FastAPI + MQTT + Token 自动监控)
├── firmware/    # ESP32-S3 终端固件 (LVGL / ES8311 音频 / 渲染引擎)
├── cli/         # Mac 终端 CLI 工具 (一键发送任务通知)
└── AGENTS.md    # 项目工程与开发规范
```

---

## 🚀 快速开始
详见 [SPEC-001: 系统架构与通信契约](specs/001-system-architecture.md)。
