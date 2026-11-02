"""Mac 宿主操作指令下发 Schema (SPEC-002 第 3.5 节)

注意: 严格依据 SPEC-002，使用 created_at / expire_at，绝不强加多余的必填 timestamp 字段。
"""

import re
from typing import Literal
from pydantic import BaseModel, ConfigDict, Field, field_validator

from cloud.app.schemas.base import BaseEnvelope


class HostCommandParams(BaseModel):
    """宿主指令参数"""
    model_config = ConfigDict(extra="ignore")

    url: str = Field(..., max_length=512, description="合规的待唤起 URL")

    @field_validator("url")
    @classmethod
    def validate_url(cls, v: str) -> str:
        if not re.match(r"^https?://[^\s;]+$", v):
            raise ValueError("URL must start with http:// or https:// and cannot contain whitespace or semicolons")
        return v


class HostCommandPayload(BaseEnvelope):
    """云端向 Mac 下发的系统指令 (sprite/host/{host_id}/down/command)"""
    type: str = Field("host_command", pattern="^host_command$")
    command_id: str = Field(..., max_length=32, description="确定性命令 ID (cmd_ + sha256(action_id)[:16])")
    source_action_id: str = Field(..., description="触发该指令的终端 source action ID")
    created_at: int = Field(..., ge=0, description="命令创建时间戳")
    expire_at: int = Field(..., ge=0, description="命令失效绝对时间戳")
    action: Literal["open_browser"] = Field("open_browser", description="固定指令动作")
    params: HostCommandParams
