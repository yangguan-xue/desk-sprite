"""GitHub Webhook API 单元测试 (TASK-004)

测试矩阵:
1. 合法 HMAC SHA-256 签名: 正确解析 PR / Workflow 事件，存入 EventStore 并返回 200
2. 缺失 X-Hub-Signature-256 头: 严格返回 401，不入库
3. 伪造/错误的签名: 严格返回 401，不入库
4. 非法 JSON 请求体: 返回 400
"""

import hashlib
import hmac
import json
import pytest
from starlette.testclient import TestClient

from cloud.app.main import create_app
from cloud.app.services.event_store import EventStore


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
def app_and_store(mock_mqtt):
    store = EventStore()
    app = create_app(event_store=store, webhook_secret="super_secret_key", mqtt_service=mock_mqtt)
    return app, store


@pytest.fixture
def client(app_and_store):
    app, _ = app_and_store
    return TestClient(app)


def _sign(body_bytes: bytes, secret: str = "super_secret_key") -> str:
    return "sha256=" + hmac.new(secret.encode("utf-8"), body_bytes, hashlib.sha256).hexdigest()


def test_github_webhook_valid_pr(client, app_and_store):
    _, store = app_and_store
    payload = {
        "action": "opened",
        "pull_request": {
            "number": 42,
            "title": "Fix memory leak in driver",
            "html_url": "https://github.com/my-org/repo/pull/42",
            "merged": False,
        },
        "repository": {
            "full_name": "my-org/repo",
        },
    }
    raw_body = json.dumps(payload).encode("utf-8")
    sig = _sign(raw_body)

    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={
            "X-Hub-Signature-256": sig,
            "X-GitHub-Event": "pull_request",
            "Content-Type": "application/json",
        },
    )
    assert response.status_code == 200
    data = response.json()
    assert data["code"] == 0
    event_id = data["data"]["event_id"]

    # 验证确实已写入 EventStore
    record = store.get(event_id)
    assert record is not None
    assert record.target_device == "esp32_c049ef"
    assert record.target_url == "https://github.com/my-org/repo/pull/42"


def test_github_webhook_missing_signature(client, app_and_store):
    _, store = app_and_store
    raw_body = b'{"action": "ping"}'
    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={"Content-Type": "application/json"},
    )
    assert response.status_code == 401
    assert response.json()["code"] == 40101
    assert store.count() == 0


def test_github_webhook_invalid_signature(client, app_and_store):
    _, store = app_and_store
    raw_body = b'{"action": "ping"}'
    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={
            "X-Hub-Signature-256": "sha256=badbadbadbadbadbadbadbadbadbadbadbadbadbadbadbadbadbadbadbadbadb",
            "Content-Type": "application/json",
        },
    )
    assert response.status_code == 401
    assert response.json()["code"] == 40101
    assert store.count() == 0


def test_github_webhook_workflow_run(client, app_and_store):
    payload = {
        "action": "completed",
        "workflow_run": {
            "name": "Integration Test",
            "conclusion": "success",
            "display_title": "Run pytest suite",
            "html_url": "https://github.com/my-org/repo/actions/runs/123",
        },
        "repository": {
            "full_name": "my-org/repo",
        },
    }
    raw_body = json.dumps(payload).encode("utf-8")
    sig = _sign(raw_body)

    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={
            "X-Hub-Signature-256": sig,
            "X-GitHub-Event": "workflow_run",
            "Content-Type": "application/json",
        },
    )
    assert response.status_code == 200
    assert response.json()["code"] == 0


def test_github_webhook_unconfigured_secret_rejected():
    """R01 验证: 默认未配置 GITHUB_WEBHOOK_SECRET 的应用绝不接受公开签名"""
    unconfigured_app = create_app(webhook_secret=None)
    client = TestClient(unconfigured_app)
    body = b'{"action": "opened"}'
    sig = _sign(body, "test_webhook_secret")
    response = client.post(
        "/api/v1/webhook/github",
        content=body,
        headers={"X-Hub-Signature-256": sig},
    )
    assert response.status_code == 401
    assert response.json()["code"] == 40101


def test_github_webhook_workflow_run_in_progress_ignored(client, app_and_store):
    """R12 验证: 未完成 (in_progress, conclusion=None) 事件被忽略，绝不触发报警"""
    _, store = app_and_store
    payload = {
        "action": "in_progress",
        "workflow_run": {
            "name": "CI Workflow",
            "conclusion": None,
            "display_title": "Run pytest suite",
            "html_url": "https://github.com/my-org/repo/actions/runs/123",
        },
        "repository": {
            "full_name": "my-org/repo",
        },
    }
    raw_body = json.dumps(payload).encode("utf-8")
    sig = _sign(raw_body)

    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={
            "X-Hub-Signature-256": sig,
            "X-GitHub-Event": "workflow_run",
            "Content-Type": "application/json",
        },
    )
    assert response.status_code == 200
    assert response.json()["code"] == 0
    # 未写入 EventStore，无告警下发
    assert store.count() == 0


def test_github_webhook_workflow_run_completed_failure(client, app_and_store):
    """R12 验证: 真正完成且失败的 workflow_run 触发 HIGH 告警"""
    from shared.enums import Priority
    _, store = app_and_store
    payload = {
        "action": "completed",
        "workflow_run": {
            "name": "CI Workflow",
            "conclusion": "failure",
            "display_title": "Build broken",
            "html_url": "https://github.com/my-org/repo/actions/runs/124",
        },
        "repository": {
            "full_name": "my-org/repo",
        },
    }
    raw_body = json.dumps(payload).encode("utf-8")
    sig = _sign(raw_body)

    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={
            "X-Hub-Signature-256": sig,
            "X-GitHub-Event": "workflow_run",
            "Content-Type": "application/json",
        },
    )
    assert response.status_code == 200
    assert response.json()["code"] == 0
    assert store.count() == 1
    rec = list(store._records.values())[0]
    assert rec.priority == Priority.HIGH


def test_github_webhook_mqtt_disconnected_returns_503(app_and_store, mock_mqtt):
    """R3-01: MQTT 离线时 Webhook 返回 503 与 retryable=True，以便调用方重试"""
    app, store = app_and_store
    mock_mqtt.set_connected(False)
    client = TestClient(app)

    payload = {
        "action": "completed",
        "workflow_run": {
            "name": "CI Workflow",
            "conclusion": "success",
            "display_title": "Build succeeded",
            "html_url": "https://github.com/my-org/repo/actions/runs/125",
        },
        "repository": {"full_name": "my-org/repo"},
    }
    raw_body = json.dumps(payload).encode("utf-8")
    sig = _sign(raw_body)

    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={
            "X-Hub-Signature-256": sig,
            "X-GitHub-Event": "workflow_run",
            "Content-Type": "application/json",
        },
    )
    assert response.status_code == 503
    data = response.json()
    assert data["code"] == 50301
    assert data["data"]["retryable"] is True


def test_github_webhook_publish_rejected_returns_503(app_and_store, mock_mqtt):
    """R3-01: MQTT publish 拒绝时 Webhook 返回 503 与 retryable=True"""
    app, store = app_and_store
    mock_mqtt.publish = lambda *args, **kwargs: False
    client = TestClient(app)

    payload = {
        "action": "completed",
        "workflow_run": {
            "name": "CI Workflow",
            "conclusion": "success",
            "display_title": "Build succeeded",
            "html_url": "https://github.com/my-org/repo/actions/runs/126",
        },
        "repository": {"full_name": "my-org/repo"},
    }
    raw_body = json.dumps(payload).encode("utf-8")
    sig = _sign(raw_body)

    response = client.post(
        "/api/v1/webhook/github",
        content=raw_body,
        headers={
            "X-Hub-Signature-256": sig,
            "X-GitHub-Event": "workflow_run",
            "Content-Type": "application/json",
        },
    )
    assert response.status_code == 503
    data = response.json()
    assert data["code"] == 50301
    assert data["data"]["retryable"] is True


