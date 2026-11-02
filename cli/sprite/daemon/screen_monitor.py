"""macOS 原生锁屏驱动与事件监听 (SPEC-004 第 3.1 节, TASK-010)

功能:
1. 监听 com.apple.screenIsLocked 广播通知 (<500ms 瞬时上报 sleep)
2. 监听 com.apple.screenIsUnlocked 广播通知 (触发瞬时前台状态刷新)
"""

import logging
import threading
from typing import Callable, Optional

logger = logging.getLogger("screen_monitor")


class ScreenMonitor:
    def __init__(
        self,
        on_locked: Optional[Callable[[], None]] = None,
        on_unlocked: Optional[Callable[[], None]] = None,
    ):
        self.on_locked = on_locked
        self.on_unlocked = on_unlocked
        self._running = False
        self._thread = None
        self._is_locked = False

    @property
    def is_locked(self) -> bool:
        return self._is_locked

    def trigger_locked_for_test(self) -> None:
        self._is_locked = True
        if self.on_locked:
            self.on_locked()

    def trigger_unlocked_for_test(self) -> None:
        self._is_locked = False
        if self.on_unlocked:
            self.on_unlocked()

    def start(self) -> None:
        self._running = True
        try:
            # 尝试使用 PyObjC 原生监听
            from Foundation import NSDistributedNotificationCenter, NSObject
            import objc

            class NotificationObserver(NSObject):
                monitor = None

                def onLock_(self, notification):
                    if self.monitor:
                        self.monitor._is_locked = True
                        if self.monitor.on_locked:
                            self.monitor.on_locked()

                def onUnlock_(self, notification):
                    if self.monitor:
                        self.monitor._is_locked = False
                        if self.monitor.on_unlocked:
                            self.monitor.on_unlocked()

            def listen_loop():
                from Foundation import NSRunLoop, NSDate
                observer = NotificationObserver.alloc().init()
                observer.monitor = self
                nc = NSDistributedNotificationCenter.defaultCenter()
                nc.addObserver_selector_name_object_(
                    observer, "onLock:", "com.apple.screenIsLocked", None
                )
                nc.addObserver_selector_name_object_(
                    observer, "onUnlock:", "com.apple.screenIsUnlocked", None
                )
                while self._running:
                    NSRunLoop.currentRunLoop().runUntilDate_(NSDate.dateWithTimeIntervalSinceNow_(1.0))

            self._thread = threading.Thread(target=listen_loop, daemon=True)
            self._thread.start()
            logger.info("ScreenMonitor started with native NSDistributedNotificationCenter")
        except Exception as e:
            logger.warning(f"Native PyObjC screen notification unavailable: {e}")

    def stop(self) -> None:
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.0)
