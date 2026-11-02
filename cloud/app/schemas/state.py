"""工作状态同步 Schema (SPEC-002 第 3.3 节)"""

from pydantic import BaseModel, ConfigDict, Field

from cloud.app.schemas.base import BaseEnvelope
from shared.enums import WorkMode


class WorkModeData(BaseModel):
    """工作模式负载"""
    model_config = ConfigDict(extra="ignore")

    work_mode: WorkMode = Field(..., description="工作模式: idle | coding | meeting | sleep")


class HostStatePayload(BaseEnvelope):
    """Mac 宿主主动上报工作状态 (sprite/host/{host_id}/up/state)"""
    type: str = Field("host_state_update", pattern="^host_state_update$")
    host_id: str = Field(..., max_length=64, description="Mac 宿主标识符")
    timestamp: int = Field(..., ge=0, description="上报时间戳")
    data: WorkModeData


class StateSyncPayload(BaseEnvelope):
    """云端向设备下发工作状态 (sprite/device/{device_id}/down/state)"""
    type: str = Field("state_sync", pattern="^state_sync$")
    timestamp: int = Field(..., ge=0, description="同步时间戳")
    data: WorkModeData
