"""Mac 执行结果回执 Schema (SPEC-002 第 3.6 节)"""

from typing import Optional
from pydantic import ConfigDict, Field

from cloud.app.schemas.base import BaseEnvelope
from shared.enums import CommandAckStatus


class CommandAckPayload(BaseEnvelope):
    """Mac 宿主执行回执体 (sprite/host/{host_id}/up/ack)"""
    type: str = Field("command_ack", pattern="^command_ack$")
    host_id: str = Field(..., max_length=64, description="上报回执的 Mac 宿主 ID")
    command_id: str = Field(..., max_length=32, description="被执行的命令 ID")
    status: CommandAckStatus = Field(..., description="执行状态: success | duplicate_ignored | expired | rejected | failed")
    error_code: int = Field(0, description="错误码，成功为 0")
    message: Optional[str] = Field(None, max_length=256, description="简要执行说明或错误描述")
    timestamp: int = Field(..., ge=0, description="回执生成时间戳")
