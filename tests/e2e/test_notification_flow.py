"""E2E-01: 通知全链路流转测试 (TASK-018, R13 真实 Broker 闭环)

链路覆盖:
Mac CLI / API `sprite notify` -> 云端鉴权与落库 (EventStore) -> 真实 Mosquitto MQTT Broker (down/alert)
-> 设备端网络接收 -> 两阶段工作模式过滤与调度状态机响应
"""

import json
import os
import socket
import subprocess
import tempfile
import threading
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


class SimulatorProcess:
    """管理真实 C 语言桌面终端模拟器 (firmware/sim/desk_sprite_sim) 子进程 (R3-06)"""

    def __init__(self, host: str, port: int, device_id: str = "esp32_c049ef"):
        sim_bin = os.path.abspath("firmware/sim/desk_sprite_sim")
        if not os.path.exists(sim_bin):
            subprocess.check_call(["make", "-C", "firmware/sim"])

        self.cmd = [
            sim_bin,
            "--headless",
            "--host", host,
            "--port", str(port),
            "--device", device_id,
        ]
        self.proc = subprocess.Popen(
            self.cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.lines: List[str] = []
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self):
        try:
            for line in iter(self.proc.stdout.readline, ""):
                if line:
                    with self._lock:
                        self.lines.append(line.strip())
        except Exception:
            pass

    def wait_for_line(self, pattern: str, timeout: float = 4.0) -> bool:
        start = time.time()
        while time.time() - start < timeout:
            with self._lock:
                for line in self.lines:
                    if pattern in line:
                        return True
            time.sleep(0.05)
        return False

    def clear_lines(self):
        with self._lock:
            self.lines.clear()

    def stop(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=2)
        except Exception:
            self.proc.kill()


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
    """构建连接真实 Broker 与实际终端模拟器进程的端到端系统上下文 (R3-06)"""
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
    # 模拟物理设备端订阅者用于辅助网络报文断言
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

    # 启动真实终端模拟器进程 (desk_sprite_sim) 并接入 MQTT
    sim = SimulatorProcess(host=broker_host, port=broker_port, device_id="esp32_c049ef")
    assert sim.wait_for_line("[Sim MQTT] Connected to Broker successfully", timeout=4.0), "Simulator failed to connect"
    assert sim.wait_for_line("Subscribed to sprite/device/esp32_c049ef/down/#", timeout=4.0), "Simulator failed to subscribe"
    time.sleep(0.3)

    # 等待真实网络连接建立完毕
    for _ in range(50):
        if mqtt_svc.is_connected and device_client.is_connected():
            break
        time.sleep(0.05)

    client = TestClient(app)
    yield client, store, received_messages, device_client, sim

    sim.stop()
    device_client.loop_stop()
    device_client.disconnect()
    mqtt_svc.stop()


def evaluate_terminal_mode_response(mode: WorkMode, priority_str: str) -> Dict[str, Any]:
    """通过编译并执行真实 C 原生 alert_filter.c 引擎验证决策结果 (模块级集成测试)"""
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


def test_alert_filter_module_integration():
    """保留底层 C 原生 alert_filter 模块矩阵验证 (R3-06 约束)"""
    d_coding = evaluate_terminal_mode_response(WorkMode.CODING, "medium")
    assert d_coding["accepted"] is True
    assert d_coding["force_mute"] is False

    d_meeting = evaluate_terminal_mode_response(WorkMode.MEETING, "medium")
    assert d_meeting["accepted"] is True
    assert d_meeting["force_mute"] is True

    d_sleep = evaluate_terminal_mode_response(WorkMode.SLEEP, "medium")
    assert d_sleep["accepted"] is False


def test_e2e_notification_coding_mode(e2e_system):
    """场景 1: Mac 终端处于 coding 模式下发起 Medium 告警 -> 真实 Broker -> 真实终端进程弹窗响铃 (R3-06)"""
    client, store, received, device_client, sim = e2e_system
    received.clear()
    sim.clear_lines()

    # 1. 向真实终端下发工作模式切换 (CODING)
    inf = device_client.publish(
        "sprite/device/esp32_c049ef/down/state",
        json.dumps({"type": "state", "work_mode": "coding"}),
        qos=1,
    )
    inf.wait_for_publish(timeout=2.0)
    assert sim.wait_for_line("[Sim Main] State mode updated: CODING", timeout=3.0)

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

    # 2. 发起 HTTP Notify 请求
    resp = client.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 200
    res_data = resp.json()
    assert res_data["code"] == 0
    event_id = res_data["data"]["event_id"]

    # 3. 检查云端 EventStore
    record = store.get(event_id)
    assert record is not None
    assert record.target_device == payload["target_device"]
    assert record.priority == Priority.MEDIUM

    # 4. 验证真实 Mosquitto Broker 上的网络报文送达
    for _ in range(30):
        if any(m.get("topic") == "sprite/device/esp32_c049ef/down/alert" for m in received):
            break
        time.sleep(0.05)

    alert_msgs = [m for m in received if m.get("topic") == "sprite/device/esp32_c049ef/down/alert"]
    assert len(alert_msgs) >= 1, "Failed to receive message over real Mosquitto broker"
    down_envelope = alert_msgs[0]["payload"]
    assert down_envelope["event_id"] == event_id

    # 5. 断言真实 C 模拟器终端进程的实际输出行为 (R3-06 核心闭环)
    assert sim.wait_for_line("[Sim Main] MQTT Alert received: CI 编译完成 [MED]", timeout=3.0)
    assert sim.wait_for_line("[Sim Main] Alert admitted to scheduler queue.", timeout=3.0)
    assert sim.wait_for_line("[Sim UI] Alert popup rendered on screen: CI 编译完成 [MED]", timeout=3.0)
    assert sim.wait_for_line("[Sim Audio] Play sound: ding", timeout=3.0)

    # 清除前序活跃弹窗，确保后续用例处于干净状态
    device_client.publish("sprite/device/esp32_c049ef/down/dismiss", "{}", qos=1)
    assert sim.wait_for_line("[Sim Main] Active alert dismissed", timeout=2.0)


def test_e2e_notification_meeting_mode_silencing(e2e_system):
    """场景 2: 终端处于 meeting 模式下收到告警 -> 真实 Broker -> 终端弹窗但强制静音 (R3-06)"""
    client, store, received, device_client, sim = e2e_system
    received.clear()
    sim.clear_lines()

    # 1. 切换终端为 MEETING 模式
    inf = device_client.publish(
        "sprite/device/esp32_c049ef/down/state",
        json.dumps({"type": "state", "work_mode": "meeting"}),
        qos=1,
    )
    inf.wait_for_publish(timeout=2.0)
    assert sim.wait_for_line("[Sim Main] State mode updated: MEETING", timeout=3.0)

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

    # 2. 发起 HTTP Notify 请求
    resp = client.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 200

    # 3. 验证真实终端模拟器进程的行为: 弹窗展示但在 meeting 模式下静音
    assert sim.wait_for_line("[Sim Main] MQTT Alert received: 会议中提醒 [MED]", timeout=3.0)
    assert sim.wait_for_line("[Sim Main] Alert admitted to scheduler queue.", timeout=3.0)
    assert sim.wait_for_line("[Sim UI] Alert popup rendered on screen: 会议中提醒 [MED]", timeout=3.0)
    assert sim.wait_for_line("[Sim Audio] Muted (PA_EN=0), skipping audio output.", timeout=3.0)

    # 清除前序活跃弹窗
    device_client.publish("sprite/device/esp32_c049ef/down/dismiss", "{}", qos=1)
    assert sim.wait_for_line("[Sim Main] Active alert dismissed", timeout=2.0)


def test_e2e_notification_sleep_mode_drop(e2e_system):
    """场景 3: 终端处于 sleep 模式下收到 Medium 告警 -> 真实 Broker -> 终端直接丢弃不亮屏 (R3-06)"""
    client, store, received, device_client, sim = e2e_system
    received.clear()
    sim.clear_lines()

    # 1. 切换终端为 SLEEP 模式
    inf = device_client.publish(
        "sprite/device/esp32_c049ef/down/state",
        json.dumps({"type": "state", "work_mode": "sleep"}),
        qos=1,
    )
    inf.wait_for_publish(timeout=2.0)
    assert sim.wait_for_line("[Sim Main] State mode updated: SLEEP", timeout=3.0)

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

    # 2. 发起 HTTP Notify 请求
    resp = client.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 200

    # 3. 验证真实终端模拟器进程的行为: 丢弃告警，不渲染弹窗，不播放声音
    assert sim.wait_for_line("[Sim Main] MQTT Alert received: 夜间普通提醒 [MED]", timeout=3.0)
    assert sim.wait_for_line("[Sim Main] Alert filtered/dropped by mode 3 or queue policy.", timeout=3.0)
    assert not sim.wait_for_line("[Sim Audio] Playing sound", timeout=0.6)
    assert not sim.wait_for_line("[Sim UI] Alert popup rendered on screen", timeout=0.6)

