"""仪表盘度量下发 Schema (SPEC-002 第 3.1 节)"""

from typing import List, Optional
from pydantic import BaseModel, ConfigDict, Field

from cloud.app.schemas.base import BaseEnvelope
from shared.enums import MetricDirection, MetricStatus, PetAction


class MetricBar(BaseModel):
    """单条仪表度量"""
    model_config = ConfigDict(extra="ignore")

    id: str = Field(..., max_length=32, description="度量指标唯一 ID")
    label: str = Field(..., min_length=1, max_length=12, description="指标标签")
    current: float = Field(..., description="当前数值")
    limit: float = Field(..., gt=0, description="总量上限，必须大于 0")
    unit: str = Field(..., min_length=1, max_length=4, description="数值单位")
    percent: int = Field(..., ge=0, le=100, description="进度百分比 0~100")
    direction: MetricDirection = Field(..., description="消耗方向: used | remaining")
    sampled_at: int = Field(..., ge=0, description="采样时间秒级时间戳")
    ttl_sec: int = Field(3600, gt=0, description="数据有效生命周期(秒)")
    status: MetricStatus = Field(..., description="指标状态: normal | warning | stale | error")


class MetricsData(BaseModel):
    """度量看板数据负载"""
    model_config = ConfigDict(extra="ignore")

    is_mock: bool = Field(False, description="是否为模拟数据")
    bars: List[MetricBar] = Field(..., min_length=1, max_length=3, description="度量指标数组，1~3 个元素")
    pet_mood_hint: Optional[PetAction] = Field(PetAction.IDLE, description="桌宠建议情绪")


class MetricsPayload(BaseEnvelope):
    """仪表盘度量下发消息体"""
    type: str = Field("metrics", pattern="^metrics$")
    timestamp: int = Field(..., ge=0, description="云端派发秒级时间戳")
    data: MetricsData
