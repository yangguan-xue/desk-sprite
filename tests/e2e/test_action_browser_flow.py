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

    # 2. 启动真实的 Mac 宿主端 DaemonMQTTWorker (RR09 真实组件)
    from cli.sprite.config import SpriteConfig
    from cli.sprite.daemon.mqtt_worker import DaemonMQTTWorker

    opened_urls: List[str] = []

    def mock_system_opener(url: str) -> bool:
        opened_urls.append(url)
        return True

    executor = CommandExecutor(
        host_id="macbook_pro",
        system_opener=mock_system_opener,
        time_provider=lambda: now,
    )
    daemon_config = SpriteConfig(
        host_id="macbook_pro",
        target_device_id="esp32_c049ef",
        mqtt_broker=broker_host,
        mqtt_port=broker_port,
    )
    daemon_worker = DaemonMQTTWorker(config=daemon_config, executor=executor)
    daemon_worker.start()

    # 启动 ACK 监控客户端，验证真实回执通道与序列化 (RR09)
    received_acks: List[Dict] = []
    ack_monitor = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ack_monitor")

    def on_ack_message(c, u, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            received_acks.append(payload)
        except Exception as e:
            pytest.fail(f"ACK payload JSON decode failed: {e}")

    ack_monitor.on_message = on_ack_message
    ack_monitor.connect(broker_host, broker_port, 60)
    ack_monitor.subscribe("sprite/host/macbook_pro/up/ack")
    ack_monitor.loop_start()

    # 3. 通过真实的 C action_reporter 生成动作报文 (RR02, RR05, RR09)
    c_source = f"""
    #include <stdio.h>
    #include "action_reporter.h"
    int main(void) {{
        device_action_t act;
        action_reporter_build_action("esp32_c049ef", GESTURE_TAP, "evt_ci_success_42", {now}, &act);
        char buf[512];
        action_reporter_serialize_json(&act, buf, sizeof(buf));
        printf("%s", buf);
        return 0;
    }}
    """
    c_file = "/tmp/probe_action_reporter.c"
    c_bin = "/tmp/probe_action_reporter"
    with open(c_file, "w") as f:
        f.write(c_source)
    subprocess.check_call([
        "gcc", "-Wall", "-Wextra", "-std=c99",
        "-I", "firmware/src/include",
        c_file, "firmware/src/engine/action_reporter.c",
        "-o", c_bin
    ])
    c_action_json = subprocess.check_output([c_bin]).decode("utf-8")
    device_action_payload = json.loads(c_action_json)
    action_uuid = device_action_payload["action_id"]

    # 启动设备 MQTT 客户端并发布真实 C 动作报文
    device_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="cid_device_esp32_c049ef")
    device_client.connect(broker_host, broker_port, 60)
    device_client.loop_start()

    # 等待所有 MQTT 客户端网络连接建立完成
    for _ in range(50):
        if cloud_mqtt.is_connected and daemon_worker.client.is_connected() and device_client.is_connected() and ack_monitor.is_connected():
            break
        time.sleep(0.05)

    # 发布由真实 C 编译输出的动作报文到 Broker
    device_client.publish(
        "sprite/device/esp32_c049ef/up/action",
        c_action_json,
        qos=1,
    )

    # 4. 等待 Mac 宿主端收到下发指令并完成打开
    for _ in range(50):
        if len(opened_urls) >= 1 and len(received_acks) >= 1:
            break
        time.sleep(0.05)

    assert len(opened_urls) == 1, "Failed to execute open_url through real broker flow"
    assert opened_urls[0] == "https://github.com/yangguan-xue/desk-sprite/pull/42"

    # 5. 验证宿主回执 ACK (RR09: 真实 ACK 上报至 up/ack，杜绝序列化异常)
    assert len(received_acks) == 1
    ack = received_acks[0]
    assert ack["type"] == "command_ack"
    assert ack["status"] == "success"
    assert ack["host_id"] == "macbook_pro"

    # 6. 验证云端事件已被标记为 CONSUMED
    rec = event_store.get("evt_ci_success_42", now=now)
    assert rec.state == EventRecordState.CONSUMED

    # 清理网络连接
    device_client.loop_stop()
    device_client.disconnect()
    ack_monitor.loop_stop()
    ack_monitor.disconnect()
    daemon_worker.stop()
    cloud_mqtt.stop()
