"""Pytest 全局测试配置与隔离 Fixtures (R01 安全基准)"""

import pytest
from starlette.testclient import TestClient

from cloud.app.main import create_app
from cloud.app.services.auth import create_test_auth_service
from cloud.app.services.event_store import EventStore
from cloud.app.services.idempotency import IdempotencyManager


@pytest.fixture
def test_auth_service():
    """显式测试凭证注册表 fixture"""
    return create_test_auth_service()


@pytest.fixture
def test_event_store():
    return EventStore()


@pytest.fixture
def test_idempotency_manager():
    return IdempotencyManager()


@pytest.fixture
def app(test_auth_service, test_event_store, test_idempotency_manager):
    """装配了显式测试凭证与隔离状态的测试应用"""
    return create_app(
        auth_service=test_auth_service,
        idempotency_manager=test_idempotency_manager,
        event_store=test_event_store,
        webhook_secret="super_secret_key",
    )


@pytest.fixture
def client(app):
    return TestClient(app)
