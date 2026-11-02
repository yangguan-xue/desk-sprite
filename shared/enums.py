"""Desk Sprite 全局枚举与常量定义 (基准规范: SPEC-002 第 2 节)"""

from enum import Enum


class Priority(str, Enum):
    """通知优先级"""
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"


class Sound(str, Enum):
    """音频标识"""
    NONE = "none"
    DING = "ding"
    CHIME = "chime"
    CHEER = "cheer"
    SIGH = "sigh"
    ALERT_BEEP = "alert_beep"


class PetAction(str, Enum):
    """桌宠动画动作标识"""
    IDLE = "idle"
    CODING = "coding"
    MEETING = "meeting"
    CHEER = "cheer"
    SIGH = "sigh"
    ALERT = "alert"
    SLEEP = "sleep"


class WorkMode(str, Enum):
    """工作模式基准状态"""
    IDLE = "idle"
    CODING = "coding"
    MEETING = "meeting"
    SLEEP = "sleep"


class DeviceActionType(str, Enum):
    """硬件按键与手势动作"""
    TAP = "tap"
    DOUBLE_TAP = "double_tap"
    LONG_PRESS = "long_press"


class CommandAckStatus(str, Enum):
    """Mac 宿主执行回执状态"""
    SUCCESS = "success"
    DUPLICATE_IGNORED = "duplicate_ignored"
    EXPIRED = "expired"
    REJECTED = "rejected"
    FAILED = "failed"


class MetricDirection(str, Enum):
    """指标看板消耗方向"""
    USED = "used"
    REMAINING = "remaining"


class MetricStatus(str, Enum):
    """指标状态健康度"""
    NORMAL = "normal"
    WARNING = "warning"
    STALE = "stale"
    ERROR = "error"


class LifecycleStatus(str, Enum):
    """在线生命周期状态"""
    ONLINE = "online"
    OFFLINE = "offline"


class EventRecordState(str, Enum):
    """云端事件存储状态"""
    PENDING = "pending"
    CONSUMED = "consumed"
    EXPIRED = "expired"
