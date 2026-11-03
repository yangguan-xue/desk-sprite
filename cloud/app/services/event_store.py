"""云端事件状态存储 (EventStore) 与有界淘汰机制 (SPEC-002 第 4 节)

契约规则:
1. 记录存储: event_id, target_device, target_host, target_url, is_interactive, created_at, state
2. 有界生命周期 (TTL):
   - Low/Medium 普通告警: 保留 10 分钟 (600 秒)
   - High 告警: 无论设备端是否手动关闭，保留上限严格设为 24 小时 (86400 秒)
3. 状态流转: pending -> consumed | expired
4. 超过 TTL 的记录自动过期淘汰，查询返回 None，杜绝幽灵唤起
5. 线程安全 (threading.RLock)
"""

import threading
import time
from dataclasses import dataclass
from typing import Callable, Dict, Optional

from shared.enums import EventRecordState, Priority


@dataclass
class EventRecord:
    event_id: str
    target_device: str
    target_host: str
    target_url: Optional[str]
    is_interactive: bool
    created_at: int
    priority: Priority = Priority.MEDIUM
    state: EventRecordState = EventRecordState.PENDING

    @property
    def ttl_seconds(self) -> int:
        """根据优先级决定有界淘汰保留时间"""
        if self.priority == Priority.HIGH:
            return 86400  # 24 小时严格有界上限 (SPEC-002 第 4.1 节)
        return 600  # 普通告警 10 分钟


class EventStore:
    """内存/线程安全的事件记录存储引擎，具备写入时自动有界清理与淘汰策略 (R10)"""

    def __init__(self, time_provider: Optional[Callable[[], int]] = None, max_records: int = 5000):
        self._time_provider = time_provider or (lambda: int(time.time()))
        self._lock = threading.RLock()
        self._records: Dict[str, EventRecord] = {}
        self._max_records = max_records

    def _now(self, now: Optional[int] = None) -> int:
        if now is not None:
            return now
        return self._time_provider()

    def save(self, record: EventRecord) -> None:
        """存储一条新事件记录，并在写入时主动触发过期淘汰和容量上限约束 (R10)"""
        with self._lock:
            # 1. 主动清理已过期事件
            self.cleanup_expired(now=record.created_at)

            # 2. 有界容量淘汰：若仍超上限，淘汰最旧记录
            if len(self._records) >= self._max_records:
                oldest_id = min(self._records.keys(), key=lambda k: self._records[k].created_at)
                del self._records[oldest_id]

            self._records[record.event_id] = record

    def get(self, event_id: str, now: Optional[int] = None) -> Optional[EventRecord]:
        """检索事件记录，若已淘汰超期则标记淘汰并返回 None"""
        current_time = self._now(now)
        with self._lock:
            record = self._records.get(event_id)
            if not record:
                return None

            # 检查是否已达到 TTL 上限
            if current_time - record.created_at > record.ttl_seconds:
                record.state = EventRecordState.EXPIRED
                del self._records[event_id]
                return None

            return record

    def mark_consumed(self, event_id: str, now: Optional[int] = None) -> bool:
        """将事件记录状态更新为 consumed"""
        with self._lock:
            record = self.get(event_id, now=now)
            if not record:
                return False
            record.state = EventRecordState.CONSUMED
            return True

    def cleanup_expired(self, now: Optional[int] = None) -> int:
        """主动清理已达到 TTL 上限的历史记录"""
        current_time = self._now(now)
        with self._lock:
            expired_ids = [
                eid
                for eid, rec in self._records.items()
                if (current_time - rec.created_at) > rec.ttl_seconds
            ]
            for eid in expired_ids:
                del self._records[eid]
            return len(expired_ids)

    def count(self) -> int:
        """当前存储中有效记录数"""
        with self._lock:
            return len(self._records)
