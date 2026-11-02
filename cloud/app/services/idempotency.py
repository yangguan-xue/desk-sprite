"""HTTP 幂等键契约管理器 (SPEC-002 第 5 节, H03 闭环)

规则:
1. 调用者隔离: owner_id + ":" + idempotency_key
2. 缓存有效期: 300 秒 (5 分钟)
3. 相同 Key + 相同 Body: 返回首次响应与 event_id，不重复下发
4. 相同 Key + 不同 Body: 严格返回 409 Conflict (code: 40901)
"""

import hashlib
import threading
import time
from dataclasses import dataclass
from typing import Callable, Dict, Optional, Tuple


class IdempotencyConflictError(Exception):
    """幂等键冲突: 相同 Key 重复提交但请求体不一致"""
    pass


@dataclass
class IdempotencyRecord:
    body_hash: str
    response: Optional[dict]
    created_at: int
    ttl_sec: int = 300


class IdempotencyManager:
    def __init__(self, time_provider: Optional[Callable[[], int]] = None):
        self._time_provider = time_provider or (lambda: int(time.time()))
        self._lock = threading.RLock()
        self._cache: Dict[str, IdempotencyRecord] = {}

    def _now(self, now: Optional[int] = None) -> int:
        if now is not None:
            return now
        return self._time_provider()

    def _make_key(self, owner_id: str, idempotency_key: str) -> str:
        return f"{owner_id}:{idempotency_key}"

    def check_or_reserve(
        self, owner_id: str, idempotency_key: str, body_bytes: bytes, now: Optional[int] = None
    ) -> Optional[dict]:
        """检查幂等记录。
        如果已存在且请求体一致，返回先前缓存的响应字典；
        如果已存在但请求体不一致，抛出 IdempotencyConflictError；
        如果不存在或已过期，占位并返回 None。
        """
        current_time = self._now(now)
        key = self._make_key(owner_id, idempotency_key)
        body_hash = hashlib.sha256(body_bytes).hexdigest()

        with self._lock:
            existing = self._cache.get(key)
            if existing:
                # 检查是否过期
                if (current_time - existing.created_at) > existing.ttl_sec:
                    del self._cache[key]
                else:
                    if existing.body_hash == body_hash:
                        return existing.response
                    else:
                        raise IdempotencyConflictError("Idempotency key conflict: payload mismatch")

            # 首次记录占位
            self._cache[key] = IdempotencyRecord(
                body_hash=body_hash, response=None, created_at=current_time
            )
            return None

    def save_response(
        self, owner_id: str, idempotency_key: str, response: dict, now: Optional[int] = None
    ) -> None:
        """保存已成功执行的响应体"""
        current_time = self._now(now)
        key = self._make_key(owner_id, idempotency_key)
        with self._lock:
            if key in self._cache:
                self._cache[key].response = response
                self._cache[key].created_at = current_time

    def cleanup_expired(self, now: Optional[int] = None) -> int:
        current_time = self._now(now)
        with self._lock:
            expired_keys = [
                k for k, v in self._cache.items() if (current_time - v.created_at) > v.ttl_sec
            ]
            for k in expired_keys:
                del self._cache[k]
            return len(expired_keys)
