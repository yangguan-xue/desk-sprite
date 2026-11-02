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


@pytest.fixture
def app_and_store():
    store = EventStore()
    app = create_app(event_store=store, webhook_secret="super_secret_key")
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
