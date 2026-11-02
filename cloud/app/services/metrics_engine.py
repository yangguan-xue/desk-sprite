"""度量看板引擎 (SPEC-001 第 1.1 节, SPEC-002 第 3.1 节, TASK-006)

功能:
1. 装配三行度量指标数据 (包含 sampled_at, ttl_sec, percent, direction, status)
2. 下发至 sprite/device/{device_id}/down/metrics (QoS 0, Retain=True)
"""

import json
import time
from typing import Callable, List, Optional

from cloud.app.schemas.metrics import MetricBar, MetricsData, MetricsPayload
from shared.enums import MetricDirection, MetricStatus, PetAction


class MetricsEngine:
    def __init__(
        self,
        mqtt_publisher: Optional[Callable[[str, str, int, bool], None]] = None,
        time_provider: Optional[Callable[[], int]] = None,
    ):
        self.mqtt_publisher = mqtt_publisher
        self._time_provider = time_provider or (lambda: int(time.time()))

    def build_mock_metrics(self, sampled_at: Optional[int] = None) -> MetricsPayload:
        now = sampled_at or self._time_provider()
        bars = [
            MetricBar(
                id="kimi_token",
                label="Kimi 5H",
                current=42000.0,
                limit=100000.0,
                unit="tok",
                percent=42,
                direction=MetricDirection.USED,
                sampled_at=now - 60,
                ttl_sec=3600,
                status=MetricStatus.NORMAL,
            ),
            MetricBar(
                id="deepseek_cost",
                label="DeepSeek月度",
                current=28.5,
                limit=100.0,
                unit="¥",
                percent=28,
                direction=MetricDirection.USED,
                sampled_at=now - 100,
                ttl_sec=3600,
                status=MetricStatus.NORMAL,
            ),
            MetricBar(
                id="copilot_sub",
                label="Copilot订阅",
                current=18.0,
                limit=30.0,
                unit="天",
                percent=60,
                direction=MetricDirection.REMAINING,
                sampled_at=max(0, now - 3700),  # 模拟已过期
                ttl_sec=3600,
                status=MetricStatus.STALE,
            ),
        ]

        return MetricsPayload(
            type="metrics",
            version=1,
            timestamp=now,
            data=MetricsData(
                is_mock=True,
                bars=bars,
                pet_mood_hint=PetAction.IDLE,
            ),
        )

    def push_metrics(self, device_id: str, payload: Optional[MetricsPayload] = None) -> MetricsPayload:
        if payload is None:
            payload = self.build_mock_metrics()

        topic = f"sprite/device/{device_id}/down/metrics"
        if self.mqtt_publisher:
            self.mqtt_publisher(topic, payload.model_dump_json(), 0, True)

        return payload
