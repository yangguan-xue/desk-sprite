"""EventStore 单元测试 (TASK-003)

覆盖范围:
1. 基础存储、检索与状态流转
2. 普通 Medium 告警 10 分钟 (600s) TTL 淘汰机制
3. High 告警 24 小时 (86400s) 严格上限有界淘汰策略
4. mark_consumed 与主动清理 cleanup_expired
"""

import pytest

from cloud.app.services.event_store import EventRecord, EventStore
from shared.enums import EventRecordState, Priority


def test_basic_save_and_get():
    store = EventStore(time_provider=lambda: 1000)
    record = EventRecord(
        event_id="evt_001",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://github.com/my-org/repo/pull/1",
        is_interactive=True,
        created_at=1000,
        priority=Priority.MEDIUM,
    )
    store.save(record)
    assert store.count() == 1

    fetched = store.get("evt_001", now=1005)
    assert fetched is not None
    assert fetched.event_id == "evt_001"
    assert fetched.state == EventRecordState.PENDING


def test_medium_alert_ttl_10m():
    """普通 Medium 告警保留 10 分钟 (600s)"""
    store = EventStore()
    record = EventRecord(
        event_id="evt_med",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://github.com/my-org/repo/pull/2",
        is_interactive=True,
        created_at=1000,
        priority=Priority.MEDIUM,
    )
    store.save(record)

    # 599 秒时依然有效
    assert store.get("evt_med", now=1000 + 599) is not None

    # 601 秒时已淘汰
    assert store.get("evt_med", now=1000 + 601) is None
    assert store.count() == 0


def test_high_alert_ttl_24h():
    """High 告警无论设备是否消除，云端严格 24 小时 (86400s) 淘汰 (H01 闭环)"""
    store = EventStore()
    record = EventRecord(
        event_id="evt_high",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://github.com/my-org/repo/pull/3",
        is_interactive=True,
        created_at=1000,
        priority=Priority.HIGH,
    )
    store.save(record)

    # 23小时 59分 (86390 秒) 依然有效
    assert store.get("evt_high", now=1000 + 86390) is not None

    # 24小时 01秒 (86401 秒) 自动淘汰，防止无限期悬挂带来的幽灵调用
    assert store.get("evt_high", now=1000 + 86401) is None
    assert store.count() == 0


def test_mark_consumed():
    store = EventStore()
    record = EventRecord(
        event_id="evt_consume",
        target_device="esp32_c049ef",
        target_host="macbook_pro",
        target_url="https://github.com/my-org/repo/pull/4",
        is_interactive=True,
        created_at=1000,
        priority=Priority.MEDIUM,
    )
    store.save(record)

    # 标记已消费
    success = store.mark_consumed("evt_consume", now=1010)
    assert success is True

    fetched = store.get("evt_consume", now=1010)
    assert fetched is not None
    assert fetched.state == EventRecordState.CONSUMED


def test_cleanup_expired():
    store = EventStore()
    store.save(EventRecord("evt_m1", "esp32_1", "mac_1", None, False, created_at=1000, priority=Priority.MEDIUM))
    store.save(EventRecord("evt_h1", "esp32_1", "mac_1", None, False, created_at=1000, priority=Priority.HIGH))

    # 在 t = 2000 时 (过 1000s)，m1 过期 (TTL 600)，h1 未过期 (TTL 86400)
    cleaned = store.cleanup_expired(now=2000)
    assert cleaned == 1
    assert store.count() == 1
    assert store.get("evt_m1", now=2000) is None
    assert store.get("evt_h1", now=2000) is not None


def test_event_store_auto_cleanup_on_write():
    """R10 验证: 写入新记录时自动触发过期清理，无需显式查询旧 ID"""
    store = EventStore()
    # 存入在 t=1000 创建的普通告警 (TTL 600)
    store.save(EventRecord("evt_old", "esp32_1", "mac_1", None, False, created_at=1000, priority=Priority.MEDIUM))
    assert store.count() == 1

    # 在 t=2000 写入新记录，旧记录应被自动淘汰
    store.save(EventRecord("evt_new", "esp32_1", "mac_1", None, False, created_at=2000, priority=Priority.MEDIUM))
    assert store.count() == 1
    # 确认内存字典中已经没有 evt_old
    assert "evt_old" not in store._records
    assert store.get("evt_new", now=2000) is not None


def test_event_store_max_capacity_bounded():
    """R10 验证: 当未过期记录超过上限时，淘汰最旧记录保持内存有界"""
    store = EventStore(max_records=3)
    for i in range(5):
        store.save(EventRecord(f"evt_{i}", "esp32_1", "mac_1", None, False, created_at=1000 + i, priority=Priority.HIGH))

    assert store.count() == 3
    # 最早的 0 和 1 应被淘汰
    assert "evt_0" not in store._records
    assert "evt_1" not in store._records
    assert "evt_2" in store._records
    assert "evt_3" in store._records
    assert "evt_4" in store._records

