"""E2E-02: 物理按键反向联动浏览器全链路测试 (TASK-019, R13 真实 Broker 闭环)

链路覆盖:
模拟终端通过真实 Mosquitto Broker 发布 device_action
-> 云端 MQTTService 接收并由 ActionRouter 校验 URL 与落库消费
-> 云端通过 Broker 向对应主机发布 host_command
-> Mac 守护进程接收并执行系统打开调用 (/usr/bin/open)
-> Mac 守护进程向 Broker 回执 ACK
"""

import json
import socket
import subprocess
import tempfile
import time
from typing import Dict, List
import pytest
import paho.mqtt.client as mqtt

from cli.sprite.daemon.command_executor import CommandExecutor
from cloud.app.services.action_router import ActionRouter
from cloud.app.services.event_store import EventRecord, EventStore
from cloud.app.services.mqtt_service import MQTTService
from shared.enums import CommandAckStatus, EventRecordState, Priority


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="module")
def real_broker():
    """启动隔离的真实 Mosquitto MQTT Broker 实例"""
    port = find_free_port()
    conf = tempfile.NamedTemporaryFile("w+", delete=False)
    conf.write(f"listener {port}\nallow_anonymous true\n")
    conf.flush()

    proc = subprocess.Popen(
        ["/opt/homebrew/sbin/mosquitto", "-c", conf.name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    for _ in range(50):
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                break
        except (OSError, ConnectionRefusedError):
            time.sleep(0.05)
    else:
        proc.terminate()
        raise RuntimeError(f"Failed to start test Mosquitto broker on port {port}")

    yield {"host": "127.0.0.1", "port": port}

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()


def test_e2e_action_to_browser_flow(real_broker):
    """全链路真实网络测试：终端动作发布 -> 真实 Broker -> 云端路由 -> 真实 Broker -> Mac Executor 执行 (R13)"""
    broker_host = real_broker["host"]
    broker_port = real_broker["port"]
    now = int(time.time())

    # 1. 启动云端存储与动作路由器
    event_store = EventStore(time_provider=lambda: now)
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

    cloud_mqtt = MQTTService(host=broker_host, port=broker_port)
    action_router = ActionRouter(
        event_store=event_store,
        mqtt_publisher=cloud_mqtt.publish,
        time_provider=lambda: now,
    )
    cloud_mqtt.action_router = action_router
    cloud_mqtt.start()

    # 2. 启动 Mac 宿主端模拟客户端 (订阅 host_command)
    received_host_commands: List[Dict] = []
    opened_urls: List[str] = []

    def mock_system_opener(url: str) -> bool:
        opened_urls.append(url)
        return True

    executor = CommandExecutor(
        host_id="macbook_pro",
        system_opener=mock_system_opener,
        time_provider=lambda: now,
    )

    host_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="macbook_pro_daemon")
    def on_host_message(c, u, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            received_host_commands.append(payload)
            # 真实由 CommandExecutor 执行
            ack = executor.execute(payload, now=now)
            # 回执 ACK 到 Broker
            c.publish("sprite/host/macbook_pro/up/status", json.dumps(ack), qos=1)
        except Exception:
            pass

    host_client.on_message = on_host_message
    host_client.connect(broker_host, broker_port, 60)
    host_client.subscribe("sprite/host/macbook_pro/down/command")
    host_client.loop_start()

    # 3. 模拟物理设备端通过网络向 Broker 发起 device_action
    device_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="esp32_c049ef_sim")
    device_client.connect(broker_host, broker_port, 60)
    device_client.loop_start()

    # 等待所有 MQTT 客户端网络连接建立完成
    for _ in range(50):
        if cloud_mqtt.is_connected and host_client.is_connected() and device_client.is_connected():
            break
        time.sleep(0.05)

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

    # 发布动作报文到 Broker
    device_client.publish(
        "sprite/device/esp32_c049ef/up/action",
        json.dumps(device_action_payload),
        qos=1,
    )

    # 4. 等待 Mac 宿主端收到下发指令并完成打开
    for _ in range(50):
        if len(opened_urls) >= 1:
            break
        time.sleep(0.05)

    assert len(opened_urls) == 1, "Failed to execute open_url through real broker flow"
    assert opened_urls[0] == "https://github.com/yangguan-xue/desk-sprite/pull/42"

    # 5. 验证 host_command 字段与状态流转
    assert len(received_host_commands) == 1
    cmd = received_host_commands[0]
    assert cmd["type"] == "host_command"
    assert cmd["action"] == "open_browser"
    assert cmd["source_action_id"] == action_uuid

    # 6. 验证云端事件已被标记为 CONSUMED
    rec = event_store.get("evt_ci_success_42", now=now)
    assert rec.state == EventRecordState.CONSUMED

    # 清理网络连接
    device_client.loop_stop()
    device_client.disconnect()
    host_client.loop_stop()
    host_client.disconnect()
    cloud_mqtt.stop()
