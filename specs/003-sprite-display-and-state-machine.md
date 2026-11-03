# SPEC-003: 桌宠视觉呈现、状态机与硬件规范

> 实机适配说明（2026-11-03）：本文硬件选型与引脚为模块化参考方案。当前 USB 小电视以 [SPEC-005](005-connected-device-system-specification.md)、[SPEC-007](007-board-hardware-validation.md)及[板级清单](../docs/hardware/2026-11-03-usb-inspection/board-profile.json)为准；显示需 Mode3，粗边界已获照片确认，单像素范围及其它方向未验收。S3原理图TP_KEY直连GPIO5，适用原生电容测量，交互/手势未通过；下文TTP223/GPIO4参考方案不能用于本机，GPIO4已用于音频输出。

- **版本**: 1.4.0 (深度闭环版)
- **状态**: 待复审
- **更新日期**: 2026-11-02
- **关联规范**: [SPEC-001 系统架构](001-system-architecture.md), [SPEC-002 通信协议](002-protocol-and-events.md), [SPEC-004 Mac客户端](004-mac-cli-and-daemon.md)

---

## 1. 硬件平台与基准选型依据

本项目第一期固定采用 **“标准模块化参考方案 (Modular Reference Build)”**：

### 1.1 模块化硬件选型清单
| 模块名称 | 具体型号 / 规格 | 关键 IC | 接口形式 | 采购与工程资料来源 |
| :--- | :--- | :---: | :---: | :--- |
| **主控开发板** | ESP32-S3-DevKitC-1-N8R8 原厂标准板 | ESP32-S3-WROOM-1 (8MB Flash, 8MB Octal PSRAM) | 双排 2.54mm 排针 | 乐鑫官方硬件手册与原理图 |
| **显示彩屏** | 1.54 寸 240×240 IPS 广视角全彩屏模块 | **ST7789V** | 标准 8-Pin SPI 接口 | 标准通用开源驱动接口 |
| **音频解码** | ES8311 低功耗音频 Codec 模块 (集成 NS4150 功放) | **ES8311** | I2S + I2C + PA_EN | 顺芯官方 ES8311 数据手册 |
| **触摸按键** | TTP223 红色电容式点动触摸按键模块 | **TTP223** | 3-Pin (VCC, GND, OUT) | 通用点动按键模块 (高电平有效) |

- **固件开发框架**: **ESP-IDF v5.1.2** (LTS 稳定版本)。
- **GUI 渲染引擎**: **LVGL v8.3.11**。
- **开发解耦策略**: 引入基于 SDL2 的 LVGL PC 模拟器编译目标。软件与 UI 开发无需等待物理焊板，可完全在 PC/Mac 上先行闭环验证。

### 1.2 基准硬件引脚连接表 (Pinout)

| 功能外设 | 信号定义 | ESP32-S3 GPIO | 电气特性与连接说明 |
| :--- | :--- | :---: | :--- |
| **ST7789V 屏幕** | SPI_MOSI / SDA | GPIO 11 | SPI 物理主机主输出 |
| | SPI_SCLK | GPIO 12 | SPI 物理主机时钟 |
| | LCD_CS | GPIO 10 | 屏幕片选，低电平有效 |
| | LCD_DC | GPIO 9 | 数据/命令控制（低命令，高数据） |
| | LCD_RST | GPIO 14 | 硬件复位引脚（上电复位脉冲） |
| | LCD_BL | GPIO 48 | 背光 PWM 调光（LEDC 通道 0） |
| **ES8311 音频** | I2S_BCLK | GPIO 15 | I2S 位时钟 |
| | I2S_LRCK | GPIO 16 | I2S 左右声道同步时钟 |
| | I2S_SDOUT | GPIO 17 | I2S 数据输出 (DAC) |
| | I2C_SDA | GPIO 18 | 寄存器配置 I2C 数据线（上拉 4.7kΩ） |
| | I2C_SCL | GPIO 8 | 寄存器配置 I2C 时钟线（上拉 4.7kΩ） |
| | PA_EN | GPIO 47 | 功放电源使能（高电平开启，低电平硬件静音） |
| **TTP223 触摸** | KEY_INPUT | GPIO 4 | 上升沿中断与高电平检测 |

---

## 2. 240×240 屏幕 UI 布局网格

屏幕物理分辨率严格为 `240 × 240` 像素。

```text
+------------------------------------------+ (0, 0)
| [Wi-Fi/状态图标]        [时钟 18:30] [电池] | -> 顶部状态栏: 高度 24px (y: 0 ~ 23)
+------------------------------------------+
|                                          |
|                                          |
|            [桌宠动态舞台区域]              | -> 精灵舞台区: 高度 144px (y: 24 ~ 167)
|        (128×128 像素居中动画展示)         |
|                                          |
|                                          |
+------------------------------------------+
| Kimi: [====--------] 42%                 | -> 仪表度量 1: 高度 24px (y: 168 ~ 191)
| DeepSeek: ¥28.5 / ¥100                   | -> 仪表度量 2: 高度 24px (y: 192 ~ 215)
| Copilot: 剩余 18 天          [STALE]     | -> 仪表度量 3: 高度 24px (y: 216 ~ 239)
+------------------------------------------+ (240, 240)
```

- **顶部状态栏 (24px)**:
  - 左侧：Wi-Fi 信号图标 + MQTT 状态圆点。
  - 中间：工作模式指示符（💻 编码 / 🔕 静音 / 🌙 休眠）。
  - 右侧：本地时间 `HH:MM`。
- **桌宠舞台区 (144px)**:
  - 舞台中心动画视口为 `128 × 128` 像素，在 `(56, 32)` 坐标居中展示。
- **三行度量指标栏 (72px)**:
  - 进度方向：`used`（反映消耗比例）、`remaining`（反映剩余比例）。
  - 本地 Stale 判定：终端检测 `now - sampled_at > ttl_sec`，进度条灰显并标示 `[STALE]`。
  - 模拟标识：`is_mock == true` 时右上角渲染 `[M]` 角标。

---

## 3. 工作模式与告警响应决策矩阵

| 当前 Base Mode | 收到 Low 告警 | 收到 Medium 告警 | 收到 High 告警 |
| :--- | :--- | :--- | :--- |
| **`coding`** (高专注写代码) | 顶部状态栏出现黄色小圆点微动 3 秒；不弹窗，不发声 | 弹出覆盖层，按 `duration_sec` 倒计时；播放对应音效；超时淡出 | 全屏警报弹窗；持续播放警报音；**无倒计时，持续闪烁直到手动按键消除** |
| **`idle`** (电脑空闲) | 状态栏打点，不弹窗 | 弹出覆盖层，按 `duration_sec` 倒计时；播放对应音效；超时淡出 | 全屏警报弹窗；持续播放警报音；直到手动按键消除 |
| **`meeting`** (会议中) | 静默丢弃，不留痕 | 弹出覆盖层，按 `duration_sec` 倒计时；**强制硬件静音 (PA_EN=0)**；超时淡出 | 全屏警报弹窗；**强制硬件静音 (PA_EN=0)**；屏幕红白交替闪烁，直到手动按键消除 |
| **`sleep`** (锁屏/夜间) | 静默丢弃，不进队 | 静默丢弃，不进队（不点亮背光） | 立即唤醒背光点亮屏幕；播放低音量警报音；直到手动按键消除 |

---

## 4. 两阶段模式过滤与队列调度契约 (H04 闭环)

为了彻底解决“工作模式静态过滤矩阵”与“告警缓冲队列调度矩阵”之间的执行时序冲突，固件严格实施 **“接收时入队过滤 (Ingress Filter) + 调度时出队复核 (Egress Re-check)”** 双重流水线：

```mermaid
flowchart TD
    Recv[MQTT 接收到新告警] --> IngressCheck{阶段 1: 接收时过滤<br/>依据当前 Base Mode}
    
    IngressCheck -- 当前为 sleep 且 (Low 或 Medium) --> IngressDrop[直接丢弃, 绝不进队, 不点亮屏幕]
    IngressCheck -- 当前为 meeting 且 Low --> IngressDrop
    IngressCheck -- 当前为 meeting 且 (Medium 或 High) --> TagMute[标记 force_mute: true]
    IngressCheck -- 当前为 coding / idle 且 Low --> StatusDot[仅状态栏打点 3 秒, 不进队]
    
    TagMute --> QueueRouter
    IngressCheck -- 当前为 coding / idle 且 (Medium 或 High) --> QueueRouter{队列路由调度}
    
    QueueRouter -- 当前屏幕正在展示 High --> HighAction{新到告警判定}
    HighAction -- 新到为 Medium --> Enqueue[压入容量为 3 的环形队列等待]
    HighAction -- 新到为同 event_id High --> DropDup[静默丢弃 (幂等)]
    HighAction -- 新到为不同 event_id High --> ReplaceHigh[立即替换当前展示内容, 重置警报]
    
    QueueRouter -- 当前屏幕正在展示 Medium --> MedAction{新到告警判定}
    MedAction -- 新到为 Medium --> Enqueue
    MedAction -- 新到为 High --> Preempt[立即中断 Medium 弹窗, 强行抢占屏幕]
    
    QueueRouter -- 当前主屏无弹窗 --> DisplayNow[立即弹出展示]
    
    %% 出队调度复核流水线
    PopupEnd[当前弹窗结束淡出 / 用户消除] --> EgressCheck{队列是否有等待告警?}
    EgressCheck -- 无等待告警 --> RecoverBase[恢复当前底层 Base Mode 渲染]
    EgressCheck -- 有等待告警 --> PopNext[取出队首待播告警]
    
    PopNext --> CheckExpire{now > alert.expire_at ?}
    CheckExpire -- 是 (已过期) --> DropExpired[静默丢弃, 取下一个]
    CheckExpire -- 否 --> RecheckMode{阶段 2: 动态复核<br/>当前最新的 Base Mode}
    
    RecheckMode -- 最新状态已变为 sleep --> DiscardAllQueue[清空并丢弃队列中所有 Medium<br/>绝不点亮屏幕, 恢复 sleep]
    RecheckMode -- 最新状态已变为 meeting --> DisplayMuted[强制 PA_EN=0 硬件静音展示]
    RecheckMode -- 最新状态仍为 coding / idle --> DisplayNormal[正常播放音效并弹窗展示]
```

### 4.1 核心执行规则总结
1. **入队严格拦截**:
   - 当终端处于 `sleep` 时，新到达的 `low` 和 `medium` **在网络层直接丢弃**，严禁压入缓冲队列。
2. **出队动态复核**:
   - 当弹窗结束准备拉取下一条排队告警时，**必须以取出瞬间最新的 Base Mode 进行复核**。若在排队期间 Mac 已锁屏进入 `sleep`，队列中残留的 `medium` 告警立即全数作废清空，绝不唤醒屏幕。
   - 若当前进入 `meeting`，出队弹窗时强制切断音频功放（`PA_EN = 0`）。
3. **展示中 High 豁免**:
   - 处于全屏警报中的 High 告警绝不受 `expire_at` 影响，必须等待物理确认。

---

## 5. 动画、字体与音频资源预算限制

- **动效显存**: PSRAM 双行缓冲区各 `240 × 40` 像素（每块约 19KB）；精灵图序列单帧 `128 × 128`，4~6 帧，播放速率 8 FPS。
- **音频数学闭环**: 统一采用 16kHz, 16-bit, 单声道 WAV 格式；时长限制在 **0.4 ~ 0.8 秒**，单个文件大小限制在 **≤ 26 KB**。
- **字库回退**: 16px UTF-8 精简点阵字库；未收录汉字安全回退渲染为方框 `□`，超长文本自动按边界截断补 `...`。

---

## 6. 物理交互与手势操作契约

针对 TTP223 单点电容触摸按键的高电平脉冲时序判定标准：

| 手势 | 当前处于 Alert 弹窗状态 | 当前处于主仪表盘状态 |
| :--- | :--- | :--- |
| **单击 (Tap, 50~300ms)** | **即刻确认**：向云端上报当前 `active_alert_id`，唤起 Mac 浏览器打开对应 URL，并立即关闭弹窗 | 切换底部三行指标看板的展示模式（详细数值 / 进度百分比） |
| **长按 (Long Press, ≥1200ms)** | 静默关闭当前弹窗（不上报打开 URL 动作） | 切换本地**临时静音 30 分钟**（状态栏显示 🔕，忽略所有 medium 音效） |
| **双击 (Double Tap)** | 忽略（由单击优先拦截） | 首版作为保留预留位（不做任何副作用操作） |

---

## 7. 验收标准与验证命令 (Verification Matrix) (H04 闭环)

| 验收层级 | 验证入口与命令 | 预期结果 |
| :--- | :--- | :--- |
| **两阶段过滤: 锁屏丢弃入队** | 模拟终端处于 `sleep` 状态下注入 `medium` 告警 | 终端直接丢弃消息，不进队，不点亮屏幕背光 |
| **两阶段复核: 排队切锁屏清空** | 在 `coding` 状态下排队 1 条 `medium`，立即向终端同步 `sleep` | 前序弹窗结束后，调度器复核检测到 `sleep`，清空排队消息，屏幕熄灭进入休眠 |
| **会议模式静音复核** | 排队 1 条带音效的 `medium`，出队前向终端同步 `meeting` | 出队弹窗展示，但 ES8311 功放使能脚 `PA_EN` 强制维持低电平（无声） |
| **纯软件/模拟器阶段** | `cd firmware/sim && make && ./desk_sprite_sim` | 成功启动 SDL2 模拟器，正确呈现 240×240 布局网格、动画循环与三行指标 |
| **固件编译构建阶段** | `cd firmware && idf.py build` | 基于 ESP-IDF v5.1.2 成功编译出 bin 镜像，RAM/Flash 分区预算未超限 |
| **实体板卡准入门槛** | `idf.py -p /dev/tty.usbserial-* flash monitor` | 实体硬件上电：ST7789V 屏幕点亮无雪花；ES8311 成功发声；TTP223 触发中断 |
