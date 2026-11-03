# TASK-001～020 第七轮复审

- 日期：2026-11-03。
- 范围：`2bcddf0..2acfeb9`，复核 R6-01 的修复、生产入口接线和新增回归测试。
- 结论：**本轮复审通过，R6-01 关闭，未发现新增阻塞问题。** 该结论针对本次变更，不代表实机与部署验收完成。
- 本轮只新增报告，未修改实现和测试、未提交；未连接板卡、操作串口或刷机，保留已有工作区与硬件调查文件。

## R6-01 关闭依据

1. `main.c` 在网络启动前初始化校时跟踪器，在 SNTP 初始化前注册成功通知回调；成功回调记录本次运行期间的已校时状态。
2. `is_time_synced()` 接入共享跟踪器。首次读到 `COMPLETED` 时也会缓存成功状态，后续底层回到 `RESET` 不再误判未校时；仍要求 Unix 时间达到有效性下限。
3. 核对本地 ESP-IDF 的 SNTP 实现：立即同步模式先更新系统时间、设置完成状态，再调用成功通知，与本次接线匹配。
4. 新测试模拟 SDK 读取 `COMPLETED` 后重置为 `RESET` 的行为，使用实际共享动作、告警调度与序列化代码，覆盖一次同步后连续两条告警，以及离线点击保留告警、恢复连接后重试成功。另验证仅有合理 Unix 时间不能绕过首次同步，成功通知后则可在 `RESET` 状态继续操作。

## 验证

| 验证 | 结果与边界 |
|---|---|
| 手势与动作 C 回归 | **6 个测试函数全部通过**，包含本轮新增的连续告警和离线重试场景；`cc -Wall -Wextra -std=c99` 编译运行 |
| ESP-IDF 启用触摸配置构建 | **退出码 0，958 步**；镜像 `0xf5dc0`，app 分区 `0x100000`，剩余 `0xa240`（约 4%），编译器报告分区容量接近上限 |
| Git whitespace 检查 | `git diff --check 2bcddf0..2acfeb9` 通过 |
| 默认触摸门禁 | 仓库 `sdkconfig` 仍默认禁用触摸动作；本轮仅在临时副本启用以编译相关生产路径 |
| 实机与部署 | 未执行真实 Broker TLS、设备动作闭环、屏幕、音频与触摸阈值验收 |

本轮没有修改 Python 服务，未重复运行上轮已通过的 Python 全套与其他无关模块测试。

C 回归编译命令（输出路径置于临时目录）：

```sh
cc -Wall -Wextra -std=c99 -I firmware/src/include \
  firmware/tests/test_touch_gestures.c \
  firmware/src/hal/touch_button.c \
  firmware/src/engine/action_reporter.c \
  firmware/src/engine/touch_action.c \
  firmware/src/engine/alert_filter.c \
  firmware/src/engine/alert_queue.c \
  -o /tmp/test_touch_gestures
/tmp/test_touch_gestures
```

固件构建复制 `firmware/` 到临时目录，排除 build、模拟器二进制与缓存，仅在副本 sdkconfig 中启用 `CONFIG_SPRITE_ENABLE_TOUCH_ACTIONS=y`，加载 `/Users/liruoxuan/esp/esp-idf/export.sh` 后执行 `idf.py build`。临时构建目录与 C 测试产物已清理，未修改仓库 sdkconfig 或构建产物。
