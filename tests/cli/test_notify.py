"""sprite notify 单元测试 (TASK-008)

测试矩阵:
1. 退出码 0: 成功下发并被云端接收
2. 退出码 1: 参数非法 (如非法 level=info, 标题超长 > 24)
3. 退出码 2: 云端鉴权失败 (401 / 403) 或网络不可达
"""

import pytest
import respx
import httpx

from cli.sprite.commands.notify import run_notify
from cli.sprite.config import SpriteConfig


@pytest.fixture
def test_config():
    return SpriteConfig(
        cloud_endpoint="http://mock-cloud.internal",
        api_key="spr_live_test_key",
        target_device_id="esp32_c049ef",
    )


@respx.mock
def test_notify_success_exit_0(test_config):
    respx.post("http://mock-cloud.internal/api/v1/notify").mock(
        return_value=httpx.Response(200, json={"code": 0, "message": "success", "data": {"event_id": "evt_1"}})
    )

    code = run_notify(
        title="CI构建通过",
        summary="全部测试通过",
        level="medium",
        sound="ding",
        mood="cheer",
        config=test_config,
    )
    assert code == 0


def test_notify_invalid_param_exit_1(test_config):
    # 非法枚举 level='info' (SPEC-004 2.1 明确禁止使用 info)
    code = run_notify(
        title="测试",
        level="info",
        config=test_config,
    )
    assert code == 1

    # 标题超长 (> 24 字符)
    code_long = run_notify(
        title="这是一条超过二十四字符长度的非法标题测试必须要拦截报错退出",
        config=test_config,
    )
    assert code_long == 1


@respx.mock
def test_notify_auth_failure_exit_2(test_config):
    respx.post("http://mock-cloud.internal/api/v1/notify").mock(
        return_value=httpx.Response(401, json={"code": 40101, "message": "Invalid API token", "data": None})
    )

    code = run_notify(title="测试", config=test_config)
    assert code == 2


@respx.mock
def test_notify_network_error_exit_2(test_config):
    respx.post("http://mock-cloud.internal/api/v1/notify").mock(
        side_effect=httpx.ConnectError("Connection refused")
    )

    code = run_notify(title="测试", config=test_config)
    assert code == 2
