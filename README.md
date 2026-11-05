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

当前 USB 小电视的实测硬件、固件状态与待验收功能，见 [SPEC-005：USB 小电视系统规格说明书](specs/005-connected-device-system-specification.md)。

腾讯云服务的安装、健康检查、凭据管理与回滚，见[云端部署说明](cloud/deploy/README.md)；本次实际部署结果见[2026-11-05 部署记录](docs/deployments/2026-11-05-tencent-cloud.md)。

显示 SPI Mode3 与 ES8311 提示音已实机核验；四边粗框及底部四色带已获照片确认。[卖家S3资料核对](docs/hardware/2026-11-03-usb-inspection/seller-materials-review.md)补齐GPIO5原生触摸、GPIO15录音、背光低有效电路；触摸交互及USB长期稳定性尚未通过。当前设备运行 Kimi 看板，仓库产品固件需先按[板级核验结果](docs/hardware/2026-11-03-usb-inspection/board-validation.md)与[引脚清单](docs/hardware/2026-11-03-usb-inspection/board-profile.json)适配。独立诊断说明见 [RAM 板级诊断工具](tools/board_probe/README.md)。
