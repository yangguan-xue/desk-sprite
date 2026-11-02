"""前台应用探测与 15 秒防抖感知引擎 (SPEC-004 第 3.1 节, TASK-010)

规则:
1. 仅在非锁屏且未处于手动覆盖 (TTL 30m) 期间进行前台探测
2. 每 3 秒检测一次前台活跃应用
3. 持续维持 15 秒后才上报状态更新 (防抖)
4. 支持测试注入时钟与前台应用提供者
"""

import logging
import time
from typing import Callable, Optional, Set

from cli.sprite.commands.state import get_active_manual_override
from shared.enums import WorkMode

logger = logging.getLogger("app_monitor")

DEFAULT_CODING_APPS: Set[str] = {
    "Code",
    "Visual Studio Code",
    "Cursor",
    "Xcode",
    "PyCharm",
    "IntelliJ IDEA",
    "Sublime Text",
    "Terminal",
    "iTerm2",
    "Ghostty",
    "Alacritty",
}

DEFAULT_MEETING_APPS: Set[str] = {
    "zoom.us",
    "TencentMeeting",
    "zoom",
    "Teams",
    "Microsoft Teams",
    "Feishu",
    "Lark",
    "DingTalk",
    "FaceTime",
    "Google Meet",
}


def get_native_frontmost_app() -> Optional[str]:
    """通过 macOS NSWorkspace 获取当前前台活跃应用名称"""
    try:
        from AppKit import NSWorkspace
        app = NSWorkspace.sharedWorkspace().frontmostApplication()
        if app:
            return app.localizedName()
    except Exception:
        pass
    return None


class AppMonitor:
    def __init__(
        self,
        on_mode_change: Optional[Callable[[WorkMode], None]] = None,
        frontmost_provider: Optional[Callable[[], Optional[str]]] = None,
        time_provider: Optional[Callable[[], int]] = None,
        debounce_sec: int = 15,
        coding_apps: Optional[Set[str]] = None,
        meeting_apps: Optional[Set[str]] = None,
    ):
        self.on_mode_change = on_mode_change
        self.frontmost_provider = frontmost_provider or get_native_frontmost_app
        self._time_provider = time_provider or (lambda: int(time.time()))
        self.debounce_sec = debounce_sec
        self.coding_apps = coding_apps or DEFAULT_CODING_APPS
        self.meeting_apps = meeting_apps or DEFAULT_MEETING_APPS

        self.current_mode: WorkMode = WorkMode.IDLE
        self.candidate_mode: Optional[WorkMode] = None
        self.candidate_first_seen: int = 0
        self.is_screen_locked: bool = False

    def _now(self, now: Optional[int] = None) -> int:
        if now is not None:
            return now
        return self._time_provider()

    def set_screen_locked(self, locked: bool, now: Optional[int] = None) -> Optional[WorkMode]:
        """由 ScreenMonitor 触发锁屏/解锁"""
        self.is_screen_locked = locked
        if locked:
            # 瞬时切入 sleep 模式
            if self.current_mode != WorkMode.SLEEP:
                self.current_mode = WorkMode.SLEEP
                self.candidate_mode = None
                if self.on_mode_change:
                    self.on_mode_change(WorkMode.SLEEP)
            return WorkMode.SLEEP
        else:
            # 解锁，立即重置并触发一次检测
            self.candidate_mode = None
            return self.poll_tick(now=now)

    def determine_app_mode(self, app_name: Optional[str]) -> WorkMode:
        if not app_name:
            return WorkMode.IDLE
        if app_name in self.coding_apps:
            return WorkMode.CODING
        if app_name in self.meeting_apps:
            return WorkMode.MEETING
        return WorkMode.IDLE

    def poll_tick(self, now: Optional[int] = None) -> WorkMode:
        """周期轮询单步 tick (通常每 3s 执行一次)"""
        current_time = self._now(now)

        # 1. 若处于锁屏状态，保持 sleep
        if self.is_screen_locked:
            return WorkMode.SLEEP

        # 2. 检查是否有处于有效期的手动锁定模式 (30m TTL)
        manual_override = get_active_manual_override(now=current_time)
        if manual_override:
            if self.current_mode != manual_override:
                self.current_mode = manual_override
                self.candidate_mode = None
                if self.on_mode_change:
                    self.on_mode_change(manual_override)
            return manual_override

        # 3. 获取前台应用并判定模式
        app_name = self.frontmost_provider()
        target_mode = self.determine_app_mode(app_name)

        # 4. 防抖判定 (Debounce)
        if target_mode == self.current_mode:
            # 候选模式重置
            self.candidate_mode = None
            self.candidate_first_seen = current_time
            return self.current_mode

        if self.candidate_mode != target_mode:
            # 首次探测到新候选模式
            self.candidate_mode = target_mode
            self.candidate_first_seen = current_time
        else:
            # 持续匹配候选模式，检查是否已达到防抖阈值 (15s)
            if (current_time - self.candidate_first_seen) >= self.debounce_sec:
                self.current_mode = target_mode
                self.candidate_mode = None
                if self.on_mode_change:
                    self.on_mode_change(target_mode)

        return self.current_mode
