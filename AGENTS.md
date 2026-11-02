# Desk Sprite (desk-sprite) — 工程规范与开发指南

## 1. 项目定位与目标
- **定位**：基于 ESP32-S3 小电视物理终端与腾讯云后端的智能桌面 AI 伴侣与物理事件中枢（Cyber Desk Companion & Physical Event Hub）。
- **核心能力**：
  - **云端主脑（Tencent Cloud）**：全天候 7×24h 运行，聚合 Kimi/LLM Token 监控、接收 Codex/CI 任务完成 Webhook、维护 MQTT 消息中心。
  - **物理精灵（ESP32-S3）**：240×240 IPS 高清像素桌宠动效、三行额度看板、ES8311 物理音频播报（清脆提示音）、触摸按键交互。
  - **多端轻量 Hook**：Mac 终端一键通知（`sprite notify`），即刻与桌面桌宠产生物理联动。

---

## 2. 规范驱动开发 (SDD) 铁律
本项目严格遵循规范驱动开发原则：
1. **无 Spec 不拆 Task**：所有系统架构、接口契约与 UI/UX 规则必须先沉淀于 `specs/` 目录。
2. **无 Task 不写代码**：开发任务原子化，每个任务有明确的输入、交付物、技术约束与可执行的验证命令。
3. **安全边界第一**：严禁将任何 API Token、长效 Refresh Token 或服务器私钥提交到 Git 仓库。
4. **Git 提交契约**：遵循 Conventional Commits 规范（`feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `chore:`）。

---

## 3. 架构分层
- `cloud/`：腾讯云端服务（FastAPI + MQTT Broker + Token Monitors）
- `firmware/`：ESP32-S3 固件（LVGL / Arduino / ESP-IDF）
- `cli/`：Mac 本地命令行与事件包装工具
- `specs/`：系统设计契约与通讯协议规范
