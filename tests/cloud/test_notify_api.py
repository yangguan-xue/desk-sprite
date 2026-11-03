"""Notify API 单元测试 (TASK-004, R01, R02)

测试矩阵:
1. 200 OK 正常下发
2. 401 Unauthorized (缺失或非法 Bearer Token)
3. 403 Forbidden (越权: Key 试图向未授权设备下发)
4. 403 Forbidden (越权: Key 试图向未授权宿主下发 - R02 跨宿主越权拦截)
5. 404 Not Found (目标设备在系统中完全不存在)
6. 409 Conflict (幂等键冲突: 同一 Idempotency-Key 提交不同 Body)
7. 幂等命中 (同一 Idempotency-Key 提交相同 Body 返回相同 event_id，不重复下发)
8. 400 Bad Request (字符超长或非法 JSON)
9. 422 Unprocessable (非法枚举或 URL 格式错误)
10. 默认未配置应用拒绝测试凭证 (R01 安全基准)
"""

import pytest
from starlette.testclient import TestClient

from cloud.app.main import create_app
from cloud.app.services.auth import create_test_auth_service
from cloud.app.services.event_store import EventStore
from cloud.app.services.idempotency import IdempotencyManager


class MockConnectedMQTT:
    def __init__(self, connected: bool = True):
        self._connected = connected
        self.published = []

    @property
    def is_connected(self) -> bool:
        return self._connected

    def set_connected(self, val: bool) -> None:
        self._connected = val

    def publish(self, topic: str, payload: str, qos: int = 1, retain: bool = False) -> bool:
        if not self._connected:
            return False
        self.published.append((topic, payload, qos, retain))
        return True


@pytest.fixture
def mock_mqtt():
    return MockConnectedMQTT(connected=True)


@pytest.fixture
def client(mock_mqtt):
    # 显式注入测试注册表与已连接 Mock MQTT (RR01)
    auth = create_test_auth_service()
    app = create_app(auth_service=auth, mqtt_service=mock_mqtt)
    return TestClient(app)


def test_notify_success(client):
    payload = {
        "target_device": "esp32_c049ef",
        "priority": "medium",
        "title": "测试通过",
        "summary": "全部 42 个用例运行完毕",
        "source": "pytest",
        "sound": "ding",
        "pet_action": "cheer",
        "duration_sec": 8,
        "url": "https://ci.example.com/reports/42",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_test_key"},
    )
    assert response.status_code == 200
    data = response.json()
    assert data["code"] == 0
    assert data["message"] == "success"
    assert data["data"]["event_id"].startswith("evt_")


def test_notify_auth_failure_401(client):
    # 1. 缺失 Token
    resp1 = client.post("/api/v1/notify", json={"target_device": "esp32_c049ef", "title": "t", "summary": "s"})
    assert resp1.status_code == 401
    assert resp1.json()["code"] == 40101

    # 2. 非法 Token
    resp2 = client.post(
        "/api/v1/notify",
        json={"target_device": "esp32_c049ef", "title": "t", "summary": "s"},
        headers={"Authorization": "Bearer invalid_secret_token"},
    )
    assert resp2.status_code == 401
    assert resp2.json()["code"] == 40101


def test_default_app_rejects_unconfigured_tokens():
    """R01 验证: 默认未配置应用绝不接受公开测试 Token"""
    default_app = create_app(auth_service=None)
    unconfigured_client = TestClient(default_app)

    response = unconfigured_client.post(
        "/api/v1/notify",
        json={"target_device": "esp32_c049ef", "title": "t", "summary": "s"},
        headers={"Authorization": "Bearer spr_live_test_key"},
    )
    assert response.status_code == 401
    assert response.json()["code"] == 40101


def test_notify_forbidden_403(client):
    # spr_live_test_key 仅被授权访问 esp32_c049ef，试图向 esp32_guest 下发必须返回 403
    payload = {
        "target_device": "esp32_guest",
        "title": "越权探测",
        "summary": "越权摘要",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_test_key"},
    )
    assert response.status_code == 403
    assert response.json()["code"] == 40301


def test_cross_host_spoofing_rejected_403(client):
    """R02 验证: 访客 Key 试图绑定未授权的目标 Mac (macbook_pro) 必须返回 403"""
    payload = {
        "target_device": "esp32_guest",
        "target_host": "macbook_pro",  # 试图跨宿主控制他人的 Mac
        "title": "恶意交互",
        "summary": "点击打开恶意网页",
        "url": "https://malicious.com",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_guest_key"},
    )
    assert response.status_code == 403
    assert response.json()["code"] == 40301
    assert "not authorized" in response.json()["message"]


def test_authorized_target_host_success(client):
    """R02 验证: 授权范围内的目标宿主允许正常下发"""
    payload = {
        "target_device": "esp32_guest",
        "target_host": "macbook_guest",  # 属于 guest Key 的合法宿主
        "title": "正常交互",
        "summary": "点击打开报表",
        "url": "https://ci.example.com",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_guest_key"},
    )
    assert response.status_code == 200
    assert response.json()["code"] == 0


def test_notify_not_found_404(client):
    payload = {
        "target_device": "esp32_non_exist_dev",
        "title": "未知设备",
        "summary": "设备不存在测试",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_test_key"},
    )
    assert response.status_code == 404
    assert response.json()["code"] == 40401


def test_notify_idempotency_conflict_409(client):
    payload_a = {
        "target_device": "esp32_c049ef",
        "title": "标题A",
        "summary": "摘要A",
    }
    payload_b = {
        "target_device": "esp32_c049ef",
        "title": "标题B",
        "summary": "摘要B",
    }
    headers = {
        "Authorization": "Bearer spr_live_test_key",
        "Idempotency-Key": "test_idem_key_12345",
    }

    # 首次提交: 200 OK
    resp_a = client.post("/api/v1/notify", json=payload_a, headers=headers)
    assert resp_a.status_code == 200
    event_id_a = resp_a.json()["data"]["event_id"]

    # 相同 Idempotency-Key 重复提交相同 Body: 仍返回 200 且返回原 event_id
    resp_a_repeat = client.post("/api/v1/notify", json=payload_a, headers=headers)
    assert resp_a_repeat.status_code == 200
    assert resp_a_repeat.json()["data"]["event_id"] == event_id_a

    # 相同 Idempotency-Key 提交不同 Body: 严格返回 409 Conflict
    resp_b = client.post("/api/v1/notify", json=payload_b, headers=headers)
    assert resp_b.status_code == 409
    assert resp_b.json()["code"] == 40901


def test_notify_validation_error_400(client):
    # 标题超长 (> 24 字符)
    payload = {
        "target_device": "esp32_c049ef",
        "title": "这是超过二十四字符长度的非法标题测试用例必须要被拦截并报错",
        "summary": "正常摘要",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_test_key"},
    )
    assert response.status_code == 400
    assert response.json()["code"] == 40001


def test_notify_malformed_url_422(client):
    payload = {
        "target_device": "esp32_c049ef",
        "title": "正常标题",
        "summary": "正常摘要",
        "url": "javascript:alert(1);",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_test_key"},
    )
    assert response.status_code == 422
    assert response.json()["code"] == 42201


def test_notify_disconnected_mqtt_returns_503_and_allows_retry(mock_mqtt):
    """RR01 回归测试: MQTT 离线或投递失败必须返回 503 且不得污染幂等缓存，重试恢复后成功"""
    auth = create_test_auth_service()
    mock_mqtt.set_connected(False)
    app = create_app(auth_service=auth, mqtt_service=mock_mqtt)
    c = TestClient(app)

    payload = {
        "target_device": "esp32_c049ef",
        "priority": "medium",
        "title": "测试断线",
        "summary": "MQTT不可用",
    }
    headers = {
        "Authorization": "Bearer spr_live_test_key",
        "Idempotency-Key": "idemp_retry_key_001",
    }

    # 1. 尝试下发，MQTT 断开返回 503
    resp = c.post("/api/v1/notify", json=payload, headers=headers)
    assert resp.status_code == 503
    data = resp.json()
    assert data["code"] == 50301
    assert data["data"]["retryable"] is True

    # 验证此时 idempotency manager 未缓存 200 成功，且已撤销占位预留
    key = app.state.idempotency_manager._make_key("user_1001", "idemp_retry_key_001")
    assert key not in app.state.idempotency_manager._cache

    # 2. Broker 恢复连接，重试相同幂等键成功投递并返回 200
    mock_mqtt.set_connected(True)
    resp2 = c.post("/api/v1/notify", json=payload, headers=headers)
    assert resp2.status_code == 200
    assert resp2.json()["code"] == 0
    assert len(mock_mqtt.published) == 1

    # 再次重试相同幂等键: 命中缓存成功
    resp3 = c.post("/api/v1/notify", json=payload, headers=headers)
    assert resp3.status_code == 200
    assert resp3.json()["data"]["event_id"] == resp2.json()["data"]["event_id"]
    assert len(mock_mqtt.published) == 1  # 未重复发送
