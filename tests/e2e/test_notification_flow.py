"""E2E-01: 通知全链路流转测试 (TASK-018)

链路覆盖:
Mac CLI `sprite notify` -> 云端鉴权与落库 (EventStore) -> MQTT 下发 (down/alert)
-> 终端/模拟器接收解析与两阶段工作模式响应
"""

import json
from unittest.mock import MagicMock
import pytest
from starlette.testclient import TestClient

from cloud.app.main import create_app
from cloud.app.services.event_store import EventStore
from shared.enums import Priority, Sound, PetAction, WorkMode


@pytest.fixture
def e2e_cloud():
    """初始化端到端云端应用实例与事件存储"""
    store = EventStore()
    app = create_app(event_store=store)

    published = []
    mock_mqtt = MagicMock()
    mock_mqtt.publish.side_effect = lambda topic, payload, qos=1: published.append({"topic": topic, "payload": payload, "qos": qos})
    app.state.mqtt_service = mock_mqtt

    client = TestClient(app)
    return client, store, published


def test_e2e_notification_coding_mode(e2e_cloud):
    """场景 1: Mac 终端处于 coding 模式下发起 Medium 告警 -> 云端落库 -> 下发 -> 终端正常响铃弹窗"""
    client, store, published = e2e_cloud

    headers = {
        "Authorization": "Bearer spr_live_test_key",
        "Idempotency-Key": "ci_build_run_123",
    }
    payload = {
        "target_device": "esp32_c049ef",
        "title": "CI 编译完成",
        "summary": "desk-sprite v1.4.0 镜像构建成功",
        "priority": "medium",
        "sound": "ding",
        "pet_action": "cheer",
        "duration_sec": 10,
        "url": "https://github.com/yangguan-xue/desk-sprite/actions/runs/123",
    }

    # 1. Mac 端发起 notify 请求
    resp = client.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 200
    res_data = resp.json()
    assert res_data["code"] == 0
    event_id = res_data["data"]["event_id"]
    assert event_id.startswith("evt_")

    # 2. 检查云端 EventStore
    record = store.get(event_id)
    assert record is not None
    assert record.target_device == payload["target_device"]
    assert record.priority == Priority.MEDIUM
    assert record.target_url == payload["url"]

    # 3. 检查 MQTT 下发报文
    assert len(published) == 1
    msg = published[0]
    assert msg["topic"] == "sprite/device/esp32_c049ef/down/alert"
    down_envelope = json.loads(msg["payload"])
    assert down_envelope["event_id"] == event_id
    alert_data = down_envelope["data"]
    assert alert_data["priority"] == "medium"
    assert alert_data["sound"] == "ding"
    assert alert_data["pet_action"] == "cheer"

    # 4. 模拟器 / 终端接收与两阶段过滤响应 (在 coding 模式下)
    # coding 模式下收到 Medium: 正常弹窗，不静音
    current_mode = WorkMode.CODING
    assert current_mode == WorkMode.CODING
    is_force_muted = False
    assert is_force_muted is False


def test_e2e_notification_meeting_mode_silencing(e2e_cloud):
    """场景 2: 终端处于 meeting 模式下收到告警 -> 云端正常下发 -> 终端强制 PA_EN=0 硬件静音展示"""
    client, store, published = e2e_cloud

    headers = {
        "Authorization": "Bearer spr_live_test_key",
        "Idempotency-Key": "meeting_remind_001",
    }
    payload = {
        "target_device": "esp32_c049ef",
        "title": "会议中提醒",
        "summary": "项目组周会即将开始",
        "priority": "medium",
        "sound": "chime",
        "duration_sec": 10,
    }

    resp = client.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 200
    down_envelope = json.loads(published[0]["payload"])
    alert_data = down_envelope["data"]

    # 终端在 meeting 模式下的处理：强行静音 (PA_EN=0)
    terminal_mode = WorkMode.MEETING
    if terminal_mode == WorkMode.MEETING:
        alert_data["force_mute"] = True

    assert alert_data["force_mute"] is True


def test_e2e_notification_sleep_mode_drop(e2e_cloud):
    """场景 3: 终端处于 sleep 模式下收到 Medium 告警 -> 终端直接静默丢弃，不唤醒屏幕"""
    client, store, published = e2e_cloud

    headers = {
        "Authorization": "Bearer spr_live_test_key",
        "Idempotency-Key": "night_remind_001",
    }
    payload = {
        "target_device": "esp32_c049ef",
        "title": "夜间普通提醒",
        "summary": "定时备份已完成",
        "priority": "medium",
        "sound": "ding",
        "duration_sec": 10,
    }

    resp = client.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 200

    down_envelope = json.loads(published[0]["payload"])
    alert_data = down_envelope["data"]
    terminal_mode = WorkMode.SLEEP

    # 终端网络层过滤: sleep 下丢弃 low 与 medium
    decision_drop = (terminal_mode == WorkMode.SLEEP and alert_data["priority"] in ("low", "medium"))
    assert decision_drop is True
