"""即时通知下发 Schema (SPEC-002 第 3.2 节)"""

import re
from typing import Optional
from pydantic import BaseModel, ConfigDict, Field, field_validator

from cloud.app.schemas.base import BaseEnvelope
from shared.enums import PetAction, Priority, Sound


class InteractiveAction(BaseModel):
    """交互式动作定义"""
    model_config = ConfigDict(extra="ignore")

    tap_action: str = Field("open_url", description="点击动作类型")
    target_url: str = Field(..., max_length=512, description="安全校验通过的跳转 URL")
    target_host: str = Field(..., max_length=64, description="接收浏览器唤起指令的 Mac 宿主 ID")

    @field_validator("target_url")
    @classmethod
    def validate_url(cls, v: str) -> str:
        if not re.match(r"^https?://[^\s;]+$", v):
            raise ValueError("URL must start with http:// or https:// and cannot contain whitespace or semicolons")
        return v


class AlertData(BaseModel):
    """即时通知数据负载"""
    model_config = ConfigDict(extra="ignore")

    category: str = Field("general", max_length=32, description="通知分类")
    priority: Priority = Field(Priority.MEDIUM, description="优先级: low | medium | high")
    title: str = Field(..., min_length=1, max_length=24, description="通知标题 1~24 字符")
    summary: str = Field(..., min_length=1, max_length=48, description="通知摘要 1~48 字符")
    source: Optional[str] = Field(None, max_length=32, description="来源标识")
    sound: Sound = Field(Sound.DING, description="音效标识")
    pet_action: PetAction = Field(PetAction.CHEER, description="桌宠伴随动作")
    duration_sec: int = Field(8, ge=3, le=60, description="展示时长 3~60 秒 (若 high 告警则忽略此项)")
    interactive: Optional[InteractiveAction] = Field(None, description="可选的可交互动作")


class AlertPayload(BaseEnvelope):
    """即时通知消息体"""
    type: str = Field("alert", pattern="^alert$")
    event_id: str = Field(..., min_length=1, max_length=64, description="全局唯一事件 ID")
    timestamp: int = Field(..., ge=0, description="产生时间戳")
    expire_at: int = Field(..., ge=0, description="过期绝对秒级时间戳")
    data: AlertData
