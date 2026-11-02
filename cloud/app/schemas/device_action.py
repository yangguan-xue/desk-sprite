"""终端物理动作上报 Schema (SPEC-002 第 3.4 节)

注意: 严格依据 SPEC-002，使用 created_at / expire_at，绝不强加多余的必填 timestamp 字段。
"""

import uuid
from typing import Optional
from pydantic import BaseModel, ConfigDict, Field, field_validator

from cloud.app.schemas.base import BaseEnvelope
from shared.enums import DeviceActionType, WorkMode


class DeviceActionData(BaseModel):
    """终端动作负载"""
    model_config = ConfigDict(extra="ignore")

    action: DeviceActionType = Field(..., description="手势动作: tap | double_tap | long_press")
    active_alert_id: Optional[str] = Field(None, max_length=64, description="当前处于交互状态的告警 ID，主屏时为 null")
    current_mode: Optional[WorkMode] = Field(None, description="上报瞬间终端处于的工作模式")


class DeviceActionPayload(BaseEnvelope):
    """终端物理按键/触摸动作上报体 (sprite/device/{device_id}/up/action)"""
    type: str = Field("device_action", pattern="^device_action$")
    action_id: str = Field(..., description="标准 36 字符 UUIDv4 动作 ID")
    device_id: str = Field(..., max_length=64, description="设备唯一标识符")
    created_at: int = Field(..., ge=0, description="动作产生时间戳")
    expire_at: int = Field(..., ge=0, description="动作截止时间戳，严格等于 created_at + 10")
    data: DeviceActionData

    @field_validator("action_id")
    @classmethod
    def validate_uuid4(cls, v: str) -> str:
        try:
            parsed = uuid.UUID(v, version=4)
            if str(parsed) != v.lower():
                raise ValueError("action_id must be a standard canonical lowercase UUIDv4 string")
        except Exception as e:
            raise ValueError(f"Invalid UUIDv4: {e}")
        return v

    @field_validator("expire_at")
    @classmethod
    def validate_expire(cls, v: int, info) -> int:
        created_at = info.data.get("created_at")
        if created_at is not None and v != created_at + 10:
            raise ValueError(f"expire_at ({v}) must strictly be created_at + 10 ({created_at + 10})")
        return v
