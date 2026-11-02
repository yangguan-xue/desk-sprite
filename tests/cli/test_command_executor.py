"""Mac 端远程指令执行器与 ACK 回执通道测试 (TASK-011)"""

import time
import pytest
from unittest.mock import MagicMock

from cli.sprite.daemon.command_executor import CommandExecutor
from shared.enums import CommandAckStatus


def test_command_executor_success():
    """正常命令执行与 ACK 生成"""
    opener = MagicMock(return_value=True)
    executor = CommandExecutor(host_id="test_mac", system_opener=opener)

    now = int(time.time())
    payload = {
        "type": "host_command",
        "version": 1,
        "command_id": "cmd_001",
        "source_action_id": "act_001",
        "created_at": now - 5,
        "expire_at": now + 60,
        "action": "open_browser",
        "params": {"url": "https://github.com/pulls"},
    }

    ack = executor.execute(payload, now=now)

    assert ack.type == "command_ack"
    assert ack.version == 1
    assert ack.host_id == "test_mac"
    assert ack.command_id == "cmd_001"
    assert ack.status == CommandAckStatus.SUCCESS
    assert ack.error_code == 0
    assert opener.call_count == 1
    opener.assert_called_once_with("https://github.com/pulls")


def test_command_executor_expired_rejection():
    """超时指令直接废弃并返回 EXPIRED，不触发 system open"""
    opener = MagicMock(return_value=True)
    executor = CommandExecutor(host_id="test_mac", system_opener=opener)

    now = 1000
    payload = {
        "type": "host_command",
        "version": 1,
        "command_id": "cmd_expired",
        "source_action_id": "act_exp",
        "created_at": 900,
        "expire_at": 950,  # now(1000) > expire_at(950)
        "action": "open_browser",
        "params": {"url": "https://github.com"},
    }

    ack = executor.execute(payload, now=now)

    assert ack.status == CommandAckStatus.EXPIRED
    assert ack.error_code == 2
    assert opener.call_count == 0


def test_command_executor_unsafe_url_rejection():
    """非法或含注入代码的 URL 被拦截"""
    opener = MagicMock(return_value=True)
    executor = CommandExecutor(host_id="test_mac", system_opener=opener)

    now = 1000
    unsafe_payloads = [
        {"url": "file:///etc/passwd"},
        {"url": "https://example.com; rm -rf /"},
        {"url": "https://example.com/ foo bar"},
        {"url": "javascript:alert(1)"},
        {"url": "https://" + "a" * 510},  # length > 512
    ]

    for p in unsafe_payloads:
        opener.reset_mock()
        payload = {
            "type": "host_command",
            "version": 1,
            "command_id": f"cmd_unsafe_{abs(hash(p['url'])) % 10000}",
            "source_action_id": "act_unsafe",
            "created_at": now,
            "expire_at": now + 60,
            "action": "open_browser",
            "params": p,
        }
        ack = executor.execute(payload, now=now)
        assert ack.status == CommandAckStatus.REJECTED
        assert ack.error_code in (1, 3)
        assert opener.call_count == 0


def test_command_executor_duplicate_in_flight_suppression():
    """并发 / 重复指令原子拦截：相同 command_id 仅调用一次 opener，后续立即返回 DUPLICATE_IGNORED"""
    opener = MagicMock(return_value=True)
    executor = CommandExecutor(host_id="test_mac", system_opener=opener)

    now = 1000
    payload = {
        "type": "host_command",
        "version": 1,
        "command_id": "cmd_dup_001",
        "source_action_id": "act_dup",
        "created_at": now,
        "expire_at": now + 60,
        "action": "open_browser",
        "params": {"url": "https://github.com/issues"},
    }

    ack1 = executor.execute(payload, now=now)
    assert ack1.status == CommandAckStatus.SUCCESS
    assert opener.call_count == 1

    ack2 = executor.execute(payload, now=now + 1)
    assert ack2.status == CommandAckStatus.DUPLICATE_IGNORED
    assert ack2.error_code == 0
    # opener 仍然只被调用 1 次
    assert opener.call_count == 1


def test_command_executor_lru_eviction():
    """LRU 容量限制测试：超出 capacity 的老指令被驱逐后可被再次记录"""
    opener = MagicMock(return_value=True)
    # 容量设为 2
    executor = CommandExecutor(host_id="test_mac", system_opener=opener, lru_capacity=2)

    now = 1000
    for i in range(1, 4):
        payload = {
            "type": "host_command",
            "version": 1,
            "command_id": f"cmd_{i}",
            "source_action_id": f"act_{i}",
            "created_at": now,
            "expire_at": now + 60,
            "action": "open_browser",
            "params": {"url": f"https://example.com/{i}"},
        }
        ack = executor.execute(payload, now=now)
        assert ack.status == CommandAckStatus.SUCCESS

    # cmd_1 应该已被逐出
    assert "cmd_1" not in executor._lru_cache
    assert "cmd_2" in executor._lru_cache
    assert "cmd_3" in executor._lru_cache
