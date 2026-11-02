"""设备与宿主生命周期监控器 (SPEC-001 第 4 节, SPEC-002 第 3.7 节, TASK-006)

核心规则:
1. 监听 sprite/host/{host_id}/up/status 与 sprite/device/{device_id}/up/status (含 LWT 遗嘱)
2. 当检测到 Mac 宿主离线超过 60 秒，自动向绑定的 ESP32 下发 work_mode: "idle"
"""

import json
import logging
import re
import time
from typing import Callable, Dict, Optional

from cloud.app.schemas.state import StateSyncPayload, WorkModeData
from cloud.app.schemas.status import StatusLifecyclePayload
from shared.enums import LifecycleStatus, WorkMode

logger = logging.getLogger("lifecycle_monitor")


class LifecycleMonitor:
    HOST_STATUS_TOPIC = re.compile(r"^sprite/host/([^/]+)/up/status$")
    DEV_STATUS_TOPIC = re.compile(r"^sprite/device/([^/]+)/up/status$")

    def __init__(
        self,
        binding_map: Optional[Dict[str, str]] = None,
        mqtt_publisher: Optional[Callable[[str, str, int, bool], None]] = None,
        time_provider: Optional[Callable[[], int]] = None,
    ):
        self.binding_map: Dict[str, str] = binding_map or {"macbook_pro": "esp32_c049ef"}
        self.mqtt_publisher = mqtt_publisher
        self._time_provider = time_provider or (lambda: int(time.time()))
        # host_id -> {"status": "online"|"offline", "offline_since": Optional[int], "idle_fallback_triggered": bool}
        self.host_states: Dict[str, dict] = {}

    def _now(self, now: Optional[int] = None) -> int:
        if now is not None:
            return now
        return self._time_provider()

    def handle_status_message(
        self, topic: str, payload_bytes: bytes, now: Optional[int] = None
    ) -> Optional[dict]:
        current_time = self._now(now)
        match_host = self.HOST_STATUS_TOPIC.match(topic)
        if not match_host:
            # 暂时关注 host 掉线退避
            return None

        host_id = match_host.group(1)
        try:
            raw_dict = json.loads(payload_bytes.decode("utf-8"))
            status_payload = StatusLifecyclePayload.model_validate(raw_dict)
        except Exception as e:
            logger.error(f"Malformed lifecycle status payload: {e}")
            return None

        state = self.host_states.setdefault(
            host_id, {"status": "online", "offline_since": None, "idle_fallback_triggered": False}
        )

        if status_payload.status == LifecycleStatus.OFFLINE:
            state["status"] = "offline"
            if state["offline_since"] is None:
                state["offline_since"] = current_time
        else:
            state["status"] = "online"
            state["offline_since"] = None
            state["idle_fallback_triggered"] = False

        return state

    def check_offline_timeouts(self, now: Optional[int] = None) -> int:
        """周期检查是否有宿主离线超 60 秒，触发退避至 idle"""
        current_time = self._now(now)
        triggered_count = 0

        for host_id, state in self.host_states.items():
            if (
                state["status"] == "offline"
                and state["offline_since"] is not None
                and not state["idle_fallback_triggered"]
            ):
                if current_time - state["offline_since"] >= 60:
                    bound_dev = self.binding_map.get(host_id)
                    if bound_dev:
                        # 触发下发 work_mode: "idle" (SPEC-001 第 4 节)
                        sync_payload = StateSyncPayload(
                            type="state_sync",
                            version=1,
                            timestamp=current_time,
                            data=WorkModeData(work_mode=WorkMode.IDLE),
                        )
                        down_topic = f"sprite/device/{bound_dev}/down/state"
                        if self.mqtt_publisher:
                            self.mqtt_publisher(down_topic, sync_payload.model_dump_json(), 1, True)
                        state["idle_fallback_triggered"] = True
                        triggered_count += 1

        return triggered_count
