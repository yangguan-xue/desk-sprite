"""StateForwarder 单元测试 (TASK-005)

测试矩阵:
1. 正常状态同步转发: Mac 上报 coding/meeting/sleep -> 准确转发至绑定设备 down/state (QoS 1, Retain=True)
2. 宿主 Topic 与 Payload 一致性校验: 不一致安全拒绝
3. 未绑定宿主处理: 跳过转发
4. 设备隔离: 严禁向无关设备下发
"""

import json
import pytest

from cloud.app.services.state_forwarder import StateForwarder
from shared.enums import WorkMode


def test_normal_state_forwarding():
    published = []

    def mock_publish(topic, payload, qos, retain):
        published.append({
            "topic": topic,
            "payload": json.loads(payload),
            "qos": qos,
            "retain": retain,
        })

    forwarder = StateForwarder(
        binding_map={"macbook_pro": "esp32_c049ef"},
        mqtt_publisher=mock_publish,
    )

    # 1. 模拟 Mac 上报 meeting
    state_payload = {
        "type": "host_state_update",
        "version": 1,
        "host_id": "macbook_pro",
        "timestamp": 1793614000,
        "data": {
            "work_mode": "meeting",
        },
    }

    result = forwarder.handle_host_state(
        topic="sprite/host/macbook_pro/up/state",
        payload_bytes=json.dumps(state_payload).encode("utf-8"),
        now=1793614001,
    )

    assert result is not None
    assert result.type == "state_sync"
    assert result.data.work_mode == WorkMode.MEETING
    assert len(published) == 1
    assert published[0]["topic"] == "sprite/device/esp32_c049ef/down/state"
    assert published[0]["qos"] == 1
    assert published[0]["retain"] is True
    assert published[0]["payload"]["data"]["work_mode"] == "meeting"


def test_spoofed_host_state_rejected():
    published = []
    forwarder = StateForwarder(
        binding_map={"macbook_pro": "esp32_c049ef"},
        mqtt_publisher=lambda t, p, q, r: published.append(t),
    )

    state_payload = {
        "type": "host_state_update",
        "version": 1,
        "host_id": "fake_host",
        "timestamp": 1793614000,
        "data": {"work_mode": "sleep"},
    }

    result = forwarder.handle_host_state(
        topic="sprite/host/macbook_pro/up/state",
        payload_bytes=json.dumps(state_payload).encode("utf-8"),
    )
    assert result is None
    assert len(published) == 0


def test_unbound_host_skipped():
    published = []
    forwarder = StateForwarder(
        binding_map={"macbook_pro": "esp32_c049ef"},
        mqtt_publisher=lambda t, p, q, r: published.append(t),
    )

    state_payload = {
        "type": "host_state_update",
        "version": 1,
        "host_id": "unbound_mac",
        "timestamp": 1793614000,
        "data": {"work_mode": "idle"},
    }

    result = forwarder.handle_host_state(
        topic="sprite/host/unbound_mac/up/state",
        payload_bytes=json.dumps(state_payload).encode("utf-8"),
    )
    assert result is None
    assert len(published) == 0
