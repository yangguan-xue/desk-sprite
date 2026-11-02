"""基础 Envelope 模型 (SPEC-002 第 3 节)

规则:
1. 仅包含全局共有字段 (type, version)
2. 开启 extra='ignore'，未知版本 (version > 1) 尽力解析已知字段，安全忽略未知字段，严禁崩溃
"""

from pydantic import BaseModel, ConfigDict, Field


class BaseEnvelope(BaseModel):
    """所有协议消息的顶层基类"""
    model_config = ConfigDict(extra="ignore")

    type: str = Field(..., description="消息类型标识")
    version: int = Field(1, ge=1, description="协议版本号，当前基准为 1")
