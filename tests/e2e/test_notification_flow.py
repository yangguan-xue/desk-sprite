"""E2E-01: 通知全链路流转测试 (TASK-018, R13 真实 Broker 闭环)

链路覆盖:
Mac CLI / API `sprite notify` -> 云端鉴权与落库 (EventStore) -> 真实 Mosquitto MQTT Broker (down/alert)
-> 设备端网络接收 -> 两阶段工作模式过滤与调度状态机响应
"""

import json
import socket
import subprocess
import tempfile
import time
from typing import Any, Dict, List, Optional
import pytest
import paho.mqtt.client as mqtt
from starlette.testclient import TestClient

from cloud.app.main import create_app
from cloud.app.services.auth import create_test_auth_service
from cloud.app.services.event_store import EventStore
from cloud.app.services.mqtt_service import MQTTService
from shared.enums import Priority, Sound, PetAction, WorkMode


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="module")
def real_broker():
    """启动隔离的真实 Mosquitto MQTT Broker 实例 (R13)"""
    port = find_free_port()
    conf = tempfile.NamedTemporaryFile("w+", delete=False)
    conf.write(f"listener {port}\nallow_anonymous true\n")
    conf.flush()

    proc = subprocess.Popen(
        ["/opt/homebrew/sbin/mosquitto", "-c", conf.name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    # 等待 broker 端口就绪
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


@pytest.fixture(scope="module")
def e2e_system(real_broker):
    """构建连接真实 Broker 的端到端系统上下文"""
    broker_host = real_broker["host"]
    broker_port = real_broker["port"]

    store = EventStore()
    mqtt_svc = MQTTService(host=broker_host, port=broker_port)
    mqtt_svc.start()

    app = create_app(
        auth_service=create_test_auth_service(),
        event_store=store,
        mqtt_service=mqtt_svc,
    )

    received_messages: List[Dict] = []
    # 模拟物理设备端订阅者
    device_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="sim_device_e2e")
    def on_msg(client, userdata, message):
        try:
            payload = json.loads(message.payload.decode("utf-8"))
            received_messages.append({"topic": message.topic, "payload": payload})
        except Exception:
            pass

    device_client.on_message = on_msg
    device_client.connect(broker_host, broker_port, 60)
    device_client.subscribe("sprite/device/+/down/#")
    device_client.loop_start()

    # 等待真实网络连接建立完毕
    for _ in range(50):
        if mqtt_svc.is_connected and device_client.is_connected():
            break
        time.sleep(0.05)

    client = TestClient(app)
    yield client, store, received_messages, device_client

    device_client.loop_stop()
    device_client.disconnect()
    mqtt_svc.stop()


def evaluate_terminal_mode_response(mode: WorkMode, priority_str: str) -> Dict[str, Any]:
    """通过编译并执行真实 C 原生 alert_filter.c 引擎验证决策结果 (RR09)"""
    import subprocess
    c_source = """
    #include <stdio.h>
    #include <string.h>
    #include <stdlib.h>
    #include "alert_filter.h"

    int main(int argc, char** argv) {
        if (argc < 3) return 1;
        work_mode_t mode = (work_mode_t)atoi(argv[1]);
        alert_payload_t alert;
        memset(&alert, 0, sizeof(alert));
        if (strcmp(argv[2], "high") == 0) alert.priority = PRIORITY_HIGH;
        else if (strcmp(argv[2], "medium") == 0) alert.priority = PRIORITY_MEDIUM;
        else alert.priority = PRIORITY_LOW;

        alert_ingress_decision_t d = alert_filter_check_ingress(mode, &alert);
        printf("{\\"decision\\":%d,\\"force_mute\\":%s}\\n",
               (int)d, alert.force_mute ? "true" : "false");
        return 0;
    }
    """
    c_file = "/tmp/probe_alert_filter.c"
    c_bin = "/tmp/probe_alert_filter"
    with open(c_file, "w") as f:
        f.write(c_source)
    subprocess.check_call([
        "gcc", "-Wall", "-Wextra", "-std=c99",
        "-I", "firmware/src/include",
        c_file, "firmware/src/engine/alert_filter.c",
        "-o", c_bin
    ])
    mode_map = {
        WorkMode.IDLE: 0,
        WorkMode.CODING: 1,
        WorkMode.MEETING: 2,
        WorkMode.SLEEP: 3,
    }
    out = subprocess.check_output([c_bin, str(mode_map[mode]), priority_str]).decode("utf-8")
    res = json.loads(out)
    return {
        "accepted": res["decision"] == 2,
        "force_mute": res["force_mute"],
        "screen_wake": res["decision"] == 2,
    }


def test_e2e_notification_coding_mode(e2e_system):
    """场景 1: Mac 终端处于 coding 模式下发起 Medium 告警 -> 真实 Broker -> 终端正常弹窗响铃 (R13)"""
    client, store, received, _ = e2e_system

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

    # 1. 发起 HTTP Notify 请求
    resp = client.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 200
    res_data = resp.json()
    assert res_data["code"] == 0
    event_id = res_data["data"]["event_id"]

    # 2. 检查云端 EventStore
    record = store.get(event_id)
    assert record is not None
    assert record.target_device == payload["target_device"]
    assert record.priority == Priority.MEDIUM

    # 3. 验证真实 Mosquitto Broker 上的网络报文送达
    for _ in range(30):
        if len(received) >= 1:
            break
        time.sleep(0.05)

    assert len(received) == 1, "Failed to receive message over real Mosquitto broker"
    msg = received[0]
    assert msg["topic"] == "sprite/device/esp32_c049ef/down/alert"
    down_envelope = msg["payload"]
    assert down_envelope["event_id"] == event_id
    alert_data = down_envelope["data"]
    assert alert_data["priority"] == "medium"
    assert alert_data["sound"] == "ding"
    assert alert_data["pet_action"] == "cheer"

    # 4. 终端在 coding 模式下的行为验证 (通过真实状态机规则推导)
    decision = evaluate_terminal_mode_response(WorkMode.CODING, alert_data["priority"])
    assert decision["accepted"] is True
    assert decision["force_mute"] is False
    assert decision["screen_wake"] is True


def test_e2e_notification_meeting_mode_silencing(e2e_system):
    """场景 2: 终端处于 meeting 模式下收到告警 -> 真实 Broker -> 终端强制 PA_EN=0 硬件静音 (R13)"""
    client, store, received, _ = e2e_system
    received.clear()

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

    # 等待真实 broker 派发
    for _ in range(30):
        if len(received) >= 1:
            break
        time.sleep(0.05)

    assert len(received) == 1
    down_envelope = received[0]["payload"]
    alert_data = down_envelope["data"]

    # 终端在 meeting 模式下的行为验证
    decision = evaluate_terminal_mode_response(WorkMode.MEETING, alert_data["priority"])
    assert decision["accepted"] is True
    assert decision["force_mute"] is True  # 真实推导出的硬件静音断言
    assert decision["screen_wake"] is True


def test_e2e_notification_sleep_mode_drop(e2e_system):
    """场景 3: 终端处于 sleep 模式下收到 Medium 告警 -> 真实 Broker -> 终端网络层丢弃不亮屏 (R13)"""
    client, store, received, _ = e2e_system
    received.clear()

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

    for _ in range(30):
        if len(received) >= 1:
            break
        time.sleep(0.05)

    assert len(received) == 1
    alert_data = received[0]["payload"]["data"]

    # 终端在 sleep 模式下的行为验证：丢弃并不唤醒屏幕
    decision = evaluate_terminal_mode_response(WorkMode.SLEEP, alert_data["priority"])
    assert decision["accepted"] is False
    assert decision["screen_wake"] is False
