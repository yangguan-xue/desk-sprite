"""Schema 单元测试 (TASK-002)

覆盖范围:
1. SPEC-002 原始示例 JSON 原样反序列化 (特别是无需 timestamp 的 device_action 和 host_command)
2. 未知版本与未知字段兼容性 (version > 1 尽力解析已知，忽略未知，严禁崩溃)
3. 严格字段边界约束 (标题、摘要、数组长度、数值范围、URL 校验等)
"""

import pytest
from pydantic import ValidationError

from cloud.app.schemas import (
    AlertPayload,
    CommandAckPayload,
    DeviceActionPayload,
    HostCommandPayload,
    HostStatePayload,
    MetricsPayload,
    StateSyncPayload,
    StatusLifecyclePayload,
)
from shared.enums import (
    CommandAckStatus,
    DeviceActionType,
    LifecycleStatus,
    MetricDirection,
    MetricStatus,
    PetAction,
    Priority,
    Sound,
    WorkMode,
)


def test_spec_metrics_example_validation():
    """验证 SPEC-002 3.1 节原始度量下发 JSON 样例"""
    sample = {
        "type": "metrics",
        "version": 1,
        "timestamp": 1793614800,
        "data": {
            "is_mock": False,
            "bars": [
                {
                    "id": "kimi_token",
                    "label": "Kimi 5H",
                    "current": 42000,
                    "limit": 100000,
                    "unit": "tok",
                    "percent": 42,
                    "direction": "used",
                    "sampled_at": 1793614740,
                    "ttl_sec": 3600,
                    "status": "normal",
                },
                {
                    "id": "deepseek_cost",
                    "label": "DeepSeek月度",
                    "current": 28.5,
                    "limit": 100.0,
                    "unit": "¥",
                    "percent": 28,
                    "direction": "used",
                    "sampled_at": 1793614700,
                    "ttl_sec": 3600,
                    "status": "normal",
                },
                {
                    "id": "copilot_sub",
                    "label": "Copilot订阅",
                    "current": 18,
                    "limit": 30,
                    "unit": "天",
                    "percent": 60,
                    "direction": "remaining",
                    "sampled_at": 1793611100,
                    "ttl_sec": 3600,
                    "status": "stale",
                },
            ],
            "pet_mood_hint": "idle",
        },
    }

    payload = MetricsPayload.model_validate(sample)
    assert payload.type == "metrics"
    assert len(payload.data.bars) == 3
    assert payload.data.bars[0].direction == MetricDirection.USED
    assert payload.data.bars[2].status == MetricStatus.STALE


def test_spec_alert_example_validation():
    """验证 SPEC-002 3.2 节原始即时通知 JSON 样例"""
    sample = {
        "type": "alert",
        "version": 1,
        "event_id": "evt_gh_20261102_001",
        "timestamp": 1793614820,
        "expire_at": 1793615120,
        "data": {
            "category": "github_pr",
            "priority": "medium",
            "title": "PR #42 构建通过",
            "summary": "耗时 3m12s · 已自动合并",
            "source": "github.com/my-org/repo",
            "sound": "ding",
            "pet_action": "cheer",
            "duration_sec": 8,
            "interactive": {
                "tap_action": "open_url",
                "target_url": "https://github.com/my-org/repo/pull/42",
                "target_host": "macbook_pro",
            },
        },
    }

    payload = AlertPayload.model_validate(sample)
    assert payload.event_id == "evt_gh_20261102_001"
    assert payload.data.priority == Priority.MEDIUM
    assert payload.data.interactive is not None
    assert payload.data.interactive.target_host == "macbook_pro"


def test_spec_device_action_example_without_timestamp():
    """验证 SPEC-002 3.4 节原始终端动作 JSON 样例 (无 timestamp 字段且不报错)"""
    sample = {
        "type": "device_action",
        "version": 1,
        "action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
        "device_id": "esp32_c049ef",
        "created_at": 1793614860,
        "expire_at": 1793614870,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_gh_20261102_001",
            "current_mode": "coding",
        },
    }

    # 严禁因为没有 timestamp 抛出校验异常
    payload = DeviceActionPayload.model_validate(sample)
    assert payload.action_id == "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b"
    assert payload.device_id == "esp32_c049ef"
    assert payload.expire_at == payload.created_at + 10
    assert payload.data.action == DeviceActionType.TAP
    assert payload.data.current_mode == WorkMode.CODING


def test_spec_host_command_example_without_timestamp():
    """验证 SPEC-002 3.5 节原始宿主指令 JSON 样例 (无 timestamp 字段且不报错)"""
    sample = {
        "type": "host_command",
        "version": 1,
        "command_id": "cmd_7540dd3e9fcadef4",
        "source_action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
        "created_at": 1793614861,
        "expire_at": 1793614921,
        "action": "open_browser",
        "params": {
            "url": "https://github.com/my-org/repo/pull/42",
        },
    }

    payload = HostCommandPayload.model_validate(sample)
    assert payload.command_id == "cmd_7540dd3e9fcadef4"
    assert payload.action == "open_browser"
    assert payload.params.url == "https://github.com/my-org/repo/pull/42"


def test_spec_command_ack_example():
    """验证 SPEC-002 3.6 节执行回执样例"""
    sample = {
        "type": "command_ack",
        "version": 1,
        "host_id": "macbook_pro",
        "command_id": "cmd_7540dd3e9fcadef4",
        "status": "success",
        "error_code": 0,
        "message": "URL opened successfully",
        "timestamp": 1793614862,
    }

    payload = CommandAckPayload.model_validate(sample)
    assert payload.status == CommandAckStatus.SUCCESS
    assert payload.error_code == 0


def test_spec_status_lifecycle_example():
    """验证 SPEC-002 3.7 节生命周期样例"""
    sample = {
        "type": "status_lifecycle",
        "version": 1,
        "status": "offline",
        "reason": "connection_lost",
        "timestamp": 1793614000,
    }

    payload = StatusLifecyclePayload.model_validate(sample)
    assert payload.status == LifecycleStatus.OFFLINE
    assert payload.reason == "connection_lost"


def test_unknown_version_and_fields_graceful_handling():
    """未知版本兼容规则: version > 1 尽力解析已知字段，忽略未知扩展字段，严禁崩溃"""
    sample = {
        "type": "alert",
        "version": 2,  # 未来更高版本
        "event_id": "evt_future_001",
        "timestamp": 1800000000,
        "expire_at": 1800000300,
        "future_field_top": {"nested": 123},  # 未知顶层字段
        "data": {
            "category": "future_cat",
            "priority": "high",
            "title": "未来告警",
            "summary": "来自版本 2 的消息",
            "future_field_inner": "ignore_me",  # 未知内层字段
            "sound": "alert_beep",
            "pet_action": "alert",
        },
    }

    payload = AlertPayload.model_validate(sample)
    assert payload.version == 2
    assert payload.event_id == "evt_future_001"
    assert payload.data.title == "未来告警"
    assert payload.data.priority == Priority.HIGH
    # 验证未知字段安全忽略
    assert not hasattr(payload, "future_field_top")


def test_field_length_and_boundary_constraints():
    """验证各项严格边界值拦截"""
    # 1. 标题超过 24 字符
    with pytest.raises(ValidationError) as exc:
        AlertPayload.model_validate({
            "type": "alert",
            "version": 1,
            "event_id": "evt_test",
            "timestamp": 1000,
            "expire_at": 2000,
            "data": {
                "title": "这是一条超过二十四字符长度的非法标题内容测试用例拦截",
                "summary": "正常摘要",
            },
        })
    assert "title" in str(exc.value)

    # 2. 摘要超过 48 字符
    with pytest.raises(ValidationError) as exc:
        AlertPayload.model_validate({
            "type": "alert",
            "version": 1,
            "event_id": "evt_test",
            "timestamp": 1000,
            "expire_at": 2000,
            "data": {
                "title": "正常标题",
                "summary": "这是非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常非常长的一个摘要超过了四十八字符限制必须拦截",
            },
        })
    assert "summary" in str(exc.value)

    # 3. 度量指标条目超过 3 个
    with pytest.raises(ValidationError) as exc:
        MetricsPayload.model_validate({
            "type": "metrics",
            "version": 1,
            "timestamp": 1000,
            "data": {
                "bars": [
                    {"id": "b1", "label": "L1", "current": 1, "limit": 10, "unit": "%", "percent": 10, "direction": "used", "sampled_at": 100, "status": "normal"},
                    {"id": "b2", "label": "L2", "current": 1, "limit": 10, "unit": "%", "percent": 10, "direction": "used", "sampled_at": 100, "status": "normal"},
                    {"id": "b3", "label": "L3", "current": 1, "limit": 10, "unit": "%", "percent": 10, "direction": "used", "sampled_at": 100, "status": "normal"},
                    {"id": "b4", "label": "L4", "current": 1, "limit": 10, "unit": "%", "percent": 10, "direction": "used", "sampled_at": 100, "status": "normal"},
                ]
            },
        })
    assert "bars" in str(exc.value)

    # 4. 非法 URL (包含分号或非 http/https)
    with pytest.raises(ValidationError) as exc:
        HostCommandPayload.model_validate({
            "type": "host_command",
            "version": 1,
            "command_id": "cmd_123",
            "source_action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
            "created_at": 1000,
            "expire_at": 1060,
            "action": "open_browser",
            "params": {
                "url": "javascript:alert(1);",
            },
        })
    assert "url" in str(exc.value)

    # 5. DeviceAction 的 expire_at 必须严格为 created_at + 10
    with pytest.raises(ValidationError) as exc:
        DeviceActionPayload.model_validate({
            "type": "device_action",
            "version": 1,
            "action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
            "device_id": "esp32_c049ef",
            "created_at": 1000,
            "expire_at": 1020,  # 非 1010
            "data": {
                "action": "tap",
            },
        })
    assert "expire_at" in str(exc.value)
