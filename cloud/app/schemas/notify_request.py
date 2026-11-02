"""Notify API 请求模型 (SPEC-002 第 5.2 节)"""

import re
from typing import Optional
from pydantic import BaseModel, ConfigDict, Field, field_validator

from shared.enums import PetAction, Priority, Sound


class NotifyRequest(BaseModel):
    model_config = ConfigDict(extra="ignore")

    target_device: str = Field(..., min_length=1, max_length=64, description="目标硬件设备 ID")
    target_host: Optional[str] = Field(None, max_length=64, description="绑定的 Mac 宿主 ID")
    priority: Priority = Field(Priority.MEDIUM, description="优先级: low | medium | high")
    title: str = Field(..., min_length=1, max_length=24, description="通知标题 1~24 字符")
    summary: str = Field(..., min_length=1, max_length=48, description="通知摘要 1~48 字符")
    source: Optional[str] = Field(None, max_length=32, description="来源标识")
    sound: Sound = Field(Sound.DING, description="音效标识")
    pet_action: PetAction = Field(PetAction.CHEER, description="桌宠伴随动作")
    duration_sec: int = Field(8, ge=3, le=60, description="展示秒数 3~60")
    url: Optional[str] = Field(None, max_length=512, description="结构化可交互 URL")

    @field_validator("url")
    @classmethod
    def validate_url(cls, v: Optional[str]) -> Optional[str]:
        if v is not None:
            if not re.match(r"^https?://[^\s;]+$", v):
                raise ValueError("URL must start with http:// or https:// and cannot contain whitespace or semicolons")
        return v
