"""生命周期监控与度量下发单元测试 (TASK-006)

测试矩阵:
1. MetricsEngine: 正常生成 1~3 条包含 sampled_at/ttl_sec 的指标，以 QoS 0, Retain=True 下发
2. LifecycleMonitor:
   - 收到 Mac LWT 遗嘱 (status: offline)
   - 离线 <60 秒: 不触发退避
   - 离线 >=60 秒: 自动向绑定设备下发 work_mode: "idle" (QoS 1, Retain=True)
   - 重新上线 (status: online): 状态重置
"""

import json
import pytest

from cloud.app.services.lifecycle_monitor import LifecycleMonitor
from cloud.app.services.metrics_engine import MetricsEngine
from shared.enums import WorkMode


def test_metrics_engine_push():
    published = []

    def mock_publish(topic, payload, qos, retain):
        published.append({
            "topic": topic,
            "payload": json.loads(payload),
            "qos": qos,
            "retain": retain,
        })

    engine = MetricsEngine(mqtt_publisher=mock_publish, time_provider=lambda: 1000)
    payload = engine.push_metrics("esp32_c049ef")

    assert payload.type == "metrics"
    assert len(payload.data.bars) == 3
    assert len(published) == 1
    assert published[0]["topic"] == "sprite/device/esp32_c049ef/down/metrics"
    assert published[0]["qos"] == 0
    assert published[0]["retain"] is True


def test_lifecycle_mac_offline_idle_fallback():
    dispatched = []

    def mock_publish(topic, payload, qos, retain):
        dispatched.append({
            "topic": topic,
            "payload": json.loads(payload),
            "qos": qos,
            "retain": retain,
        })

    monitor = LifecycleMonitor(
        binding_map={"macbook_pro": "esp32_c049ef"},
        mqtt_publisher=mock_publish,
    )

    # 1. 收到 Mac 离线 LWT 遗嘱消息 (t = 1000)
    lwt_payload = {
        "type": "status_lifecycle",
        "version": 1,
        "status": "offline",
        "reason": "connection_lost",
        "timestamp": 1000,
    }
    monitor.handle_status_message(
        topic="sprite/host/macbook_pro/up/status",
        payload_bytes=json.dumps(lwt_payload).encode("utf-8"),
        now=1000,
    )

    # 2. 离线 50 秒后 (t = 1050): 不应触发退避
    triggered = monitor.check_offline_timeouts(now=1050)
    assert triggered == 0
    assert len(dispatched) == 0

    # 3. 离线 60 秒后 (t = 1060): 触发退避至 idle
    triggered = monitor.check_offline_timeouts(now=1060)
    assert triggered == 1
    assert len(dispatched) == 1
    assert dispatched[0]["topic"] == "sprite/device/esp32_c049ef/down/state"
    assert dispatched[0]["qos"] == 1
    assert dispatched[0]["retain"] is True
    assert dispatched[0]["payload"]["data"]["work_mode"] == "idle"

    # 4. 离线 70 秒后 (t = 1070): 不应重复触发
    triggered_again = monitor.check_offline_timeouts(now=1070)
    assert triggered_again == 0
    assert len(dispatched) == 1

    # 5. Mac 重新上线 (t = 1100)
    online_payload = {
        "type": "status_lifecycle",
        "version": 1,
        "status": "online",
        "client_version": "1.4.0",
        "timestamp": 1100,
    }
    monitor.handle_status_message(
        topic="sprite/host/macbook_pro/up/status",
        payload_bytes=json.dumps(online_payload).encode("utf-8"),
        now=1100,
    )
    assert monitor.host_states["macbook_pro"]["status"] == "online"
    assert monitor.host_states["macbook_pro"]["offline_since"] is None
