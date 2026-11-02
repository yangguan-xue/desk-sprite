"""E2E-02: 物理按键反向联动浏览器全链路测试 (TASK-019)

链路覆盖:
模拟器/终端按键上报 action -> ActionRouter 归属与 URL 校验 -> 组装 host_command ->
Mac Daemon 执行 (/usr/bin/open) 并回执 ACK
"""

import json
from unittest.mock import MagicMock
import pytest

from cli.sprite.daemon.command_executor import CommandExecutor
from cloud.app.services.action_router import ActionRouter
from cloud.app.services.event_store import EventRecord, EventStore
from shared.enums import CommandAckStatus, EventRecordState, Priority


def test_e2e_action_to_browser_flow():
    """全链路测试：终端 Tap -> ActionRouter 生成指令 -> Mac Executor 执行并生成回执"""
    now = 1000

    # 1. 准备云端服务
    event_store = EventStore(time_provider=lambda: now)

    # 预先在云端落库一条待交互的事件
    event_store.save(
        EventRecord(
            event_id="evt_ci_success_42",
            target_device="esp32_c049ef",
            target_host="macbook_pro",
            target_url="https://github.com/yangguan-xue/desk-sprite/pull/42",
            is_interactive=True,
            created_at=now - 5,
            priority=Priority.MEDIUM,
            state=EventRecordState.PENDING,
        )
    )

    # 捕获云端向 MQTT 发布的消息
    cloud_published_commands = []
    def mock_mqtt_publish(topic, payload, qos=1):
        cloud_published_commands.append({"topic": topic, "payload": json.loads(payload), "qos": qos})

    router = ActionRouter(
        event_store=event_store,
        mqtt_publisher=mock_mqtt_publish,
        time_provider=lambda: now,
    )

    # 2. 模拟终端通过按键产生并上报符合 SPEC-002 的规范 device_action 报文
    action_uuid = "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d"
    device_action_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": action_uuid,
        "device_id": "esp32_c049ef",
        "created_at": now,
        "expire_at": now + 10,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_ci_success_42",
        },
    }

    # 触发云端 ActionRouter 路由处理
    result = router.handle_device_action(
        topic="sprite/device/esp32_c049ef/up/action",
        payload_bytes=json.dumps(device_action_payload).encode("utf-8"),
        now=now,
    )
    assert result.status == "command_dispatched"
    assert result.target_host == "macbook_pro"

    # 验证云端事件已被标记为 CONSUMED，杜绝幽灵二次唤起
    rec = event_store.get("evt_ci_success_42", now=now)
    assert rec.state == EventRecordState.CONSUMED

    # 验证云端下发了向对应 Mac 主机的 host_command
    assert len(cloud_published_commands) == 1
    cmd_msg = cloud_published_commands[0]
    assert cmd_msg["topic"] == "sprite/host/macbook_pro/down/command"

    host_command_payload = cmd_msg["payload"]
    assert host_command_payload["type"] == "host_command"
    assert host_command_payload["action"] == "open_browser"
    assert host_command_payload["source_action_id"] == action_uuid
    assert host_command_payload["params"]["url"] == "https://github.com/yangguan-xue/desk-sprite/pull/42"

    # 3. 模拟 Mac 守护进程 Executor 接收并安全执行
    mock_opener = MagicMock(return_value=True)
    mac_executor = CommandExecutor(
        host_id="macbook_pro",
        system_opener=mock_opener,
        time_provider=lambda: now,
    )

    ack = mac_executor.execute(host_command_payload, now=now)

    # 验证 Mac 本地安全调用与 ACK 回执
    assert mock_opener.call_count == 1
    mock_opener.assert_called_once_with("https://github.com/yangguan-xue/desk-sprite/pull/42")

    assert ack.type == "command_ack"
    assert ack.host_id == "macbook_pro"
    assert ack.command_id == host_command_payload["command_id"]
    assert ack.status == CommandAckStatus.SUCCESS
    assert ack.error_code == 0
