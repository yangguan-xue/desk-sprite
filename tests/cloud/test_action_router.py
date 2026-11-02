"""ActionRouter 单元测试 (TASK-005, H01 闭环)

测试矩阵:
1. 正常交互链路: 终端点击 -> 云端校验归属 -> 确定性计算 command_id -> 派发 host_command -> 标记 consumed
2. 跨设备冒名安全拦截: 设备 B 试图点击设备 A 的告警 -> 归属校验失败 -> 绝不派发 host_command
3. Topic 与 Payload 实体 ID 伪造拦截: Topic 提取与 Payload device_id 不一致 -> 直接拦截
4. 纯文本通知消除: 无 URL 的通知 -> 点击仅标记 consumed，绝不派发 host_command
5. 淘汰超期告警点击: >24h 告警在云端已淘汰 -> 点击不生成指令，杜绝幽灵调用
6. 确定性哈希计算与 10 分钟去重缓存: 验证 SPEC-002 line 190 哈希结果及二次重复派发拦截
"""

import json
import pytest

from cloud.app.services.action_router import ActionRouter
from cloud.app.services.event_store import EventRecord, EventStore
from shared.enums import EventRecordState, Priority


@pytest.fixture
def store():
    return EventStore()


def test_normal_action_dispatch(store):
    dispatched = []

    def mock_publish(topic, payload, qos):
        dispatched.append({"topic": topic, "payload": json.loads(payload), "qos": qos})

    router = ActionRouter(event_store=store, mqtt_publisher=mock_publish)

    # 预存一条可交互告警
    record = EventRecord(
        event_id="evt_gh_001",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://github.com/my-org/repo/pull/42",
        is_interactive=True,
        created_at=1000,
        priority=Priority.MEDIUM,
    )
    store.save(record)

    action_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
        "device_id": "esp32_c049ef",
        "created_at": 1005,
        "expire_at": 1015,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_gh_001",
            "current_mode": "coding",
        },
    }

    result = router.handle_device_action(
        topic="sprite/device/esp32_c049ef/up/action",
        payload_bytes=json.dumps(action_payload).encode("utf-8"),
        now=1006,
    )

    assert result.status == "command_dispatched"
    assert result.command is not None
    # 验证 SPEC-002 确切哈希值 cmd_7540dd3e9fcadef4
    assert result.command.command_id == "cmd_7540dd3e9fcadef4"
    assert result.command.params.url == "https://github.com/my-org/repo/pull/42"
    assert len(dispatched) == 1
    assert dispatched[0]["topic"] == "sprite/host/macbook_pro/down/command"

    # 验证 EventStore 状态更新为 consumed
    rec = store.get("evt_gh_001", now=1006)
    assert rec.state == EventRecordState.CONSUMED


def test_cross_device_spoofing_rejected(store):
    """跨设备冒名安全拦截 (H01 闭环): 设备 B 试图操作设备 A 的告警"""
    dispatched = []
    router = ActionRouter(event_store=store, mqtt_publisher=lambda t, p, q: dispatched.append(t))

    # 设备 A 的告警
    record = EventRecord(
        event_id="evt_device_a",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://secret.example.com",
        is_interactive=True,
        created_at=1000,
    )
    store.save(record)

    # 设备 B 发出动作
    action_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": "9a7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8c",
        "device_id": "esp32_attacker",
        "created_at": 1005,
        "expire_at": 1015,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_device_a",
        },
    }

    result = router.handle_device_action(
        topic="sprite/device/esp32_attacker/up/action",
        payload_bytes=json.dumps(action_payload).encode("utf-8"),
        now=1006,
    )

    assert result.status == "ownership_rejected"
    assert len(dispatched) == 0  # 绝不下发 host_command


def test_topic_and_payload_mismatch_rejected(store):
    """Topic 与 Payload 实体 ID 伪造拦截"""
    router = ActionRouter(event_store=store)

    action_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": "9a7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8c",
        "device_id": "esp32_fake",
        "created_at": 1005,
        "expire_at": 1015,
        "data": {"action": "tap"},
    }

    result = router.handle_device_action(
        topic="sprite/device/esp32_c049ef/up/action",
        payload_bytes=json.dumps(action_payload).encode("utf-8"),
        now=1006,
    )
    assert result.status == "spoofing_rejected"


def test_plain_alert_dismissed_without_command(store):
    """无 URL 的纯文本通知点击只标记 consumed，不产生 host_command"""
    dispatched = []
    router = ActionRouter(event_store=store, mqtt_publisher=lambda t, p, q: dispatched.append(t))

    record = EventRecord(
        event_id="evt_plain",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url=None,
        is_interactive=False,
        created_at=1000,
    )
    store.save(record)

    action_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": "11111111-2222-4333-8444-555555555555",
        "device_id": "esp32_c049ef",
        "created_at": 1005,
        "expire_at": 1015,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_plain",
        },
    }

    result = router.handle_device_action(
        topic="sprite/device/esp32_c049ef/up/action",
        payload_bytes=json.dumps(action_payload).encode("utf-8"),
        now=1006,
    )
    assert result.status == "plain_alert_dismissed"
    assert len(dispatched) == 0


def test_expired_alert_tap_rejected(store):
    """超期淘汰 (>24h) 告警被点击时安全拒绝派发命令"""
    dispatched = []
    router = ActionRouter(event_store=store, mqtt_publisher=lambda t, p, q: dispatched.append(t))

    record = EventRecord(
        event_id="evt_old",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://github.com/my-org/repo/pull/1",
        is_interactive=True,
        created_at=1000,
        priority=Priority.HIGH,
    )
    store.save(record)

    # 模拟在 24 小时零 10 秒后点击 (86410s 后)
    action_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": "22222222-3333-4444-8555-666666666666",
        "device_id": "esp32_c049ef",
        "created_at": 1000 + 86410,
        "expire_at": 1000 + 86420,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_old",
        },
    }

    result = router.handle_device_action(
        topic="sprite/device/esp32_c049ef/up/action",
        payload_bytes=json.dumps(action_payload).encode("utf-8"),
        now=1000 + 86411,
    )
    assert result.status == "event_not_found_or_expired"
    assert len(dispatched) == 0


def test_deduplication_cache_prevents_duplicate_dispatch(store):
    """10 分钟内重复相同的 action_id / command_id 拦截"""
    dispatched = []
    router = ActionRouter(event_store=store, mqtt_publisher=lambda t, p, q: dispatched.append(t))

    record = EventRecord(
        event_id="evt_dup",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://github.com/my-org/repo/pull/10",
        is_interactive=True,
        created_at=1000,
    )
    store.save(record)

    action_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": "33333333-4444-4555-8666-777777777777",
        "device_id": "esp32_c049ef",
        "created_at": 1005,
        "expire_at": 1015,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_dup",
        },
    }

    # 首次派发: 成功
    res1 = router.handle_device_action(
        topic="sprite/device/esp32_c049ef/up/action",
        payload_bytes=json.dumps(action_payload).encode("utf-8"),
        now=1006,
    )
    assert res1.status == "command_dispatched"
    assert len(dispatched) == 1

    # 再次重复派发: 被去重缓存拦截
    res2 = router.handle_device_action(
        topic="sprite/device/esp32_c049ef/up/action",
        payload_bytes=json.dumps(action_payload).encode("utf-8"),
        now=1008,
    )
    assert res2.status == "duplicate_skipped"
    assert len(dispatched) == 1
