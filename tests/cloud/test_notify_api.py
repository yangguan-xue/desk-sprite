"""Notify API 单元测试 (TASK-004)

测试矩阵:
1. 200 OK 正常下发
2. 401 Unauthorized (缺失或非法 Bearer Token)
3. 403 Forbidden (越权: Key 试图向未授权设备下发)
4. 404 Not Found (目标设备在系统中完全不存在)
5. 409 Conflict (幂等键冲突: 同一 Idempotency-Key 提交不同 Body)
6. 幂等命中 (同一 Idempotency-Key 提交相同 Body 返回相同 event_id，不重复下发)
7. 400 Bad Request (字符超长或非法 JSON)
8. 422 Unprocessable (非法枚举或 URL 格式错误)
"""

import pytest
from starlette.testclient import TestClient

from cloud.app.main import create_app
from cloud.app.services.auth import AuthService
from cloud.app.services.event_store import EventStore
from cloud.app.services.idempotency import IdempotencyManager


@pytest.fixture
def client():
    app = create_app()
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
        headers={"Authorization": "Bearer wrong_token"},
    )
    assert resp2.status_code == 401
    assert resp2.json()["code"] == 40101


def test_notify_forbidden_403(client):
    # 使用 guest key 试图访问 dev 设备 (esp32_c049ef 属于 owner_dev，不属于 guest)
    payload = {
        "target_device": "esp32_c049ef",
        "title": "越权探测",
        "summary": "测试越权",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_guest_key"},
    )
    assert response.status_code == 403
    assert response.json()["code"] == 40301
    assert "Forbidden" in response.json()["message"]


def test_notify_not_found_404(client):
    # 目标设备不存在
    payload = {
        "target_device": "esp32_non_exist_dev",
        "title": "不存在设备",
        "summary": "测试不存在",
    }
    response = client.post(
        "/api/v1/notify",
        json=payload,
        headers={"Authorization": "Bearer spr_live_test_key"},
    )
    assert response.status_code == 404
    assert response.json()["code"] == 40401
    assert "not found" in response.json()["message"]


def test_notify_idempotency_conflict_409(client):
    headers = {
        "Authorization": "Bearer spr_live_test_key",
        "Idempotency-Key": "test-key-001",
    }
    payload_a = {
        "target_device": "esp32_c049ef",
        "title": "第一次提交标题",
        "summary": "摘要 A",
    }
    payload_b = {
        "target_device": "esp32_c049ef",
        "title": "第二次不同标题",
        "summary": "摘要 B",
    }

    # 首次提交: 200
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
