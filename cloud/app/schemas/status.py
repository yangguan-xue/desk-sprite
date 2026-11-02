"""在线生命周期状态 Schema (SPEC-002 第 3.7 节)"""

from typing import Optional
from pydantic import Field

from cloud.app.schemas.base import BaseEnvelope
from shared.enums import LifecycleStatus


class StatusLifecyclePayload(BaseEnvelope):
    """在线生命周期通知与遗嘱消息 (sprite/.../up/status)"""
    type: str = Field("status_lifecycle", pattern="^status_lifecycle$")
    status: LifecycleStatus = Field(..., description="生命周期状态: online | offline")
    reason: Optional[str] = Field(None, max_length=64, description="离线原因: connection_lost | graceful_shutdown 等")
    client_version: Optional[str] = Field(None, max_length=32, description="客户端版本号")
    timestamp: int = Field(..., ge=0, description="状态发生时间戳")
