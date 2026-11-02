"""Desk Sprite 数据契约与 Pydantic Schema 汇聚 (SPEC-002 第 3 节)"""

from cloud.app.schemas.alert import AlertData, AlertPayload, InteractiveAction
from cloud.app.schemas.base import BaseEnvelope
from cloud.app.schemas.command_ack import CommandAckPayload
from cloud.app.schemas.device_action import DeviceActionData, DeviceActionPayload
from cloud.app.schemas.host_command import HostCommandParams, HostCommandPayload
from cloud.app.schemas.metrics import MetricBar, MetricsData, MetricsPayload
from cloud.app.schemas.state import HostStatePayload, StateSyncPayload, WorkModeData
from cloud.app.schemas.status import StatusLifecyclePayload

__all__ = [
    "BaseEnvelope",
    "MetricBar",
    "MetricsData",
    "MetricsPayload",
    "InteractiveAction",
    "AlertData",
    "AlertPayload",
    "WorkModeData",
    "HostStatePayload",
    "StateSyncPayload",
    "DeviceActionData",
    "DeviceActionPayload",
    "HostCommandParams",
    "HostCommandPayload",
    "CommandAckPayload",
    "StatusLifecyclePayload",
]
