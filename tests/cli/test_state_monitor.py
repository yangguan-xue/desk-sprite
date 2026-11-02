"""Mac 状态感知与 15 秒防抖单元测试 (TASK-010)

测试矩阵:
1. 锁屏瞬时驱动: 触发锁屏立即切入 sleep
2. 前台应用 15 秒防抖: 候选模式持续 15 秒才上报，抖动中间断则重新计时
3. 手动模式覆盖 (30m TTL): 手动锁定期间忽略前台应用探测
"""

import pytest

from cli.sprite.commands.state import clear_manual_override, set_manual_override
from cli.sprite.daemon.app_monitor import AppMonitor
from cli.sprite.daemon.screen_monitor import ScreenMonitor
from shared.enums import WorkMode


@pytest.fixture(autouse=True)
def clean_override():
    clear_manual_override()
    yield
    clear_manual_override()


def test_screen_lock_instant_sleep():
    reported = []
    monitor = AppMonitor(on_mode_change=lambda m: reported.append(m))
    assert monitor.current_mode == WorkMode.IDLE

    # 触发锁屏
    mode = monitor.set_screen_locked(True)
    assert mode == WorkMode.SLEEP
    assert monitor.current_mode == WorkMode.SLEEP
    assert len(reported) == 1
    assert reported[0] == WorkMode.SLEEP

    # 解锁并恢复
    monitor.set_screen_locked(False)
    assert monitor.is_screen_locked is False


def test_15s_debounce_for_coding_app():
    reported = []
    current_app = "Finder"

    def get_app():
        return current_app

    monitor = AppMonitor(
        on_mode_change=lambda m: reported.append(m),
        frontmost_provider=get_app,
        debounce_sec=15,
    )

    # t = 1000: 初始在 Finder (idle)
    assert monitor.poll_tick(now=1000) == WorkMode.IDLE
    assert len(reported) == 0

    # t = 1003: 切换到 VS Code
    current_app = "Code"
    assert monitor.poll_tick(now=1003) == WorkMode.IDLE  # 刚进入，尚未达到 15s 防抖
    assert len(reported) == 0

    # t = 1012 (经过 9 秒): 仍在 Code
    assert monitor.poll_tick(now=1012) == WorkMode.IDLE
    assert len(reported) == 0

    # t = 1018 (经过 15 秒): 达到防抖阈值，正式切入 coding
    assert monitor.poll_tick(now=1018) == WorkMode.CODING
    assert len(reported) == 1
    assert reported[0] == WorkMode.CODING


def test_debounce_resets_on_app_jitter():
    reported = []
    current_app = "Finder"

    monitor = AppMonitor(
        on_mode_change=lambda m: reported.append(m),
        frontmost_provider=lambda: current_app,
        debounce_sec=15,
    )

    # t = 1000: 进入 Cursor
    current_app = "Cursor"
    monitor.poll_tick(now=1000)

    # t = 1010 (经过 10 秒): 中途切回了 Finder (抖动)
    current_app = "Finder"
    monitor.poll_tick(now=1010)

    # t = 1013: 又切回 Cursor
    current_app = "Cursor"
    monitor.poll_tick(now=1013)

    # t = 1020 (距离 1013 仅 7 秒): 未达到 15 秒，依然是 IDLE
    assert monitor.poll_tick(now=1020) == WorkMode.IDLE
    assert len(reported) == 0

    # t = 1028 (距离 1013 达到 15 秒): 稳定转入 CODING
    assert monitor.poll_tick(now=1028) == WorkMode.CODING
    assert len(reported) == 1


def test_manual_override_precedence():
    reported = []
    current_app = "Code"  # 前台在写代码

    monitor = AppMonitor(
        on_mode_change=lambda m: reported.append(m),
        frontmost_provider=lambda: current_app,
        debounce_sec=15,
    )

    # 手动锁定 meeting 30 分钟
    set_manual_override("meeting", ttl_sec=1800)

    # 轮询探测: 即使前台是 Code，也严格服从手动覆盖
    assert monitor.poll_tick(now=1000) == WorkMode.MEETING
    assert len(reported) == 1
    assert reported[0] == WorkMode.MEETING

    # 清除手动锁定后，恢复前台探测
    clear_manual_override()
    # 重新经过 15 秒防抖切换
    monitor.poll_tick(now=1010)
    assert monitor.poll_tick(now=1026) == WorkMode.CODING
