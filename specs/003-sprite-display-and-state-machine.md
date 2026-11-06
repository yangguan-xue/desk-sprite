# SPEC-003: 桌宠视觉呈现、状态机与硬件规范

> 实机适配说明（2026-11-03）：本文硬件选型与引脚为模块化参考方案。当前 USB 小电视以 [SPEC-005](005-connected-device-system-specification.md)、[SPEC-007](007-board-hardware-validation.md)及[板级清单](../docs/hardware/2026-11-03-usb-inspection/board-profile.json)为准；显示需 Mode3，粗边界已获照片确认，单像素范围及其它方向未验收。S3原理图TP_KEY直连GPIO5，适用原生电容测量，交互/手势未通过；下文TTP223/GPIO4参考方案不能用于本机，GPIO4已用于音频输出。

- **版本**: 1.5.0 (樱花汽水页面)
- **状态**: 待复审
- **更新日期**: 2026-11-05
- **关联规范**: [SPEC-001 系统架构](001-system-architecture.md), [SPEC-002 通信协议](002-protocol-and-events.md), [SPEC-004 Mac客户端](004-mac-cli-and-daemon.md)

> 动作系统补充（2026-11-06）：[SPEC-011](011-pet-action-system.md)定义后续角色姿态、动作生命周期、资源预算和验收；尚未实现。等待队列容量统一为当前实现的 5 条，不含当前展示项；本次仅对齐文档。

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

## 2. 240×240 屏幕 UI 布局（2026-11-05 樱花汽水版）

本节替代旧版深色三行看板及旧版告警视觉效果（闪烁、淡出）；模式过滤、优先级与确认规则不变。实际显示采用共享 C 像素渲染器，ESP32 与 SDL2 输出一致。

- 色板：背景 `#fff8fc`、正文 `#63384f`、主色 `#ef4287`、窗框 `#ffc6df`。
- y=0..36：花朵、SAKURA、真实本地时钟、Wi-Fi/MQTT 在线与静音状态。
- y=43..184：拱窗、花盆、茶杯边桌；原 sakura-green 67×120 透明像素角色，以最近邻显示为 78×140，脚底 y=178。
- y=187..211：白色圆角对话条；idle「我在，慢慢来。」、coding「你写代码，我陪你。」、meeting「安静陪你开会。」、sleep「明天也一起加油。」。
- y=219..239：通用「剩余额度」和单条进度，不显示订阅厂商品牌。选择第一项有效额度；used 换算 remaining；未知 total、非法数值不伪造百分比。过期灰显并提示，mock 明示「演示」。触摸数值/百分比切换保留。
- coding 加小电脑，medium cheer 告警保留房间并加星光，对话条显示实际摘要，底栏临时显示实际标题与倒计时；关闭后恢复额度。sleep 使用紫色夜景、闭眼角色。高优先级告警覆盖房间，显示标题、摘要与手动确认；medium 倒计时；动作不可用时不显示按键承诺。告警关闭后恢复基础模式。
- 文字独立用 12px 单色字库逐像素绘制；中文未收录字使用方框，不拆 UTF-8。消息限制在指定区域，末尾省略。
- 动画目标 8 fps，眨眼周期 5.3s、持续约 225ms，呼吸一像素。时钟未同步显示 `--:--`。
- 按 16 行 RGB565 条带同步输出，调用方提供 7680 字节缓冲区，不申请完整帧缓冲；SPI 提交前转换大端字节序，复用现有 DMA 同步屏障。
- 本次不改变板级引脚、协议、网络认证或告警调度，不自动烧录。

---

## 3. 工作模式与告警响应决策矩阵

| 当前 Base Mode | 收到 Low 告警 | 收到 Medium 告警 | 收到 High 告警 |
| :--- | :--- | :--- | :--- |
| **`coding`** (高专注写代码) | 顶部状态栏出现黄色小圆点微动 3 秒；不弹窗，不发声 | 展示通知，按 `duration_sec` 倒计时；播放对应音效；超时关闭 | 全屏警报弹窗；持续播放警报音；**无倒计时，直到手动确认消除** |
| **`idle`** (电脑空闲) | 状态栏打点，不弹窗 | 展示通知，按 `duration_sec` 倒计时；播放对应音效；超时关闭 | 全屏警报弹窗；持续播放警报音；直到手动按键消除 |
| **`meeting`** (会议中) | 静默丢弃，不留痕 | 展示通知，按 `duration_sec` 倒计时；**强制硬件静音 (PA_EN=0)**；超时关闭 | 全屏警报弹窗；**强制硬件静音 (PA_EN=0)**；直到手动确认消除 |
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
    HighAction -- 新到为 Medium --> Enqueue[压入容量为 5 的等待队列]
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

- **动效显存**: 渲染缓冲区 `240 × 16 × 2 = 7680` 字节；SPI 字节序转换另用同尺寸静态 DMA 缓冲区。两帧原角色与字库以只读常量存储，目标 8 FPS。
- **音频数学闭环**: 统一采用 16kHz, 16-bit, 单声道 WAV 格式；时长限制在 **0.4 ~ 0.8 秒**，单个文件大小限制在 **≤ 26 KB**。
- **字库回退**: 12px UTF-8 精简点阵字库；未收录汉字安全回退渲染为方框 `□`，超长文本自动按边界截断补 `…`。

---

## 6. 物理交互与手势操作契约

针对 TTP223 单点电容触摸按键的高电平脉冲时序判定标准：

| 手势 | 当前处于 Alert 弹窗状态 | 当前处于主仪表盘状态 |
| :--- | :--- | :--- |
| **单击 (Tap, 50~300ms)** | **即刻确认**：向云端上报当前 `active_alert_id`，唤起 Mac 浏览器打开对应 URL，并立即关闭弹窗 | 切换底部额度的展示模式（详细数值 / 进度百分比） |
| **长按 (Long Press, ≥1200ms)** | 静默关闭当前弹窗（不上报打开 URL 动作） | 切换本地**临时静音 30 分钟**（状态栏显示 🔕，忽略所有 medium 音效） |
| **双击 (Double Tap)** | 忽略（由单击优先拦截） | 首版作为保留预留位（不做任何副作用操作） |

---

## 7. 验收标准与验证命令 (Verification Matrix) (H04 闭环)

| 验收层级 | 验证入口与命令 | 预期结果 |
| :--- | :--- | :--- |
| **两阶段过滤: 锁屏丢弃入队** | 模拟终端处于 `sleep` 状态下注入 `medium` 告警 | 终端直接丢弃消息，不进队，不点亮屏幕背光 |
| **两阶段复核: 排队切锁屏清空** | 在 `coding` 状态下排队 1 条 `medium`，立即向终端同步 `sleep` | 前序弹窗结束后，调度器复核检测到 `sleep`，清空排队消息，屏幕熄灭进入休眠 |
| **会议模式静音复核** | 排队 1 条带音效的 `medium`，出队前向终端同步 `meeting` | 出队弹窗展示，但 ES8311 功放使能脚 `PA_EN` 强制维持低电平（无声） |
| **纯软件/模拟器阶段** | `cd firmware/sim && make && ./desk_sprite_sim` | 成功启动 SDL2 模拟器，正确呈现 240×240 樱花汽水页面、动画循环与剩余额度 |
| **固件编译构建阶段** | `cd firmware && idf.py build` | 基于 ESP-IDF v5.1.2 成功编译出 bin 镜像，RAM/Flash 分区预算未超限 |
| **实体板卡准入门槛** | `idf.py -p /dev/tty.usbserial-* flash monitor` | 实体硬件上电：ST7789V 屏幕点亮无雪花；ES8311 成功发声；TTP223 触发中断 |
