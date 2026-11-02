"""Mac 工作状态接收与下发转发器 (SPEC-002 第 3.3 节, TASK-005)

功能:
1. 监听 sprite/host/{host_id}/up/state
2. 校验 Topic host_id 与 Payload 一致性
3. 依据静态绑定映射 (host_id -> target_device)，组装 state_sync
4. 严格定向下发至 sprite/device/{target_device}/down/state (QoS 1, Retain=True)
5. 杜绝向无关设备泄漏转发
"""

import json
import logging
import re
from typing import Callable, Dict, Optional

from cloud.app.schemas.state import HostStatePayload, StateSyncPayload, WorkModeData
from shared.enums import WorkMode

logger = logging.getLogger("state_forwarder")


class StateForwarder:
    TOPIC_PATTERN = re.compile(r"^sprite/host/([^/]+)/up/state$")

    def __init__(
        self,
        binding_map: Optional[Dict[str, str]] = None,
        mqtt_publisher: Optional[Callable[[str, str, int, bool], None]] = None,
    ):
        # 默认静态一对一绑定 (SPEC-001 1.1 节)
        self.binding_map: Dict[str, str] = binding_map or {
            "macbook_pro": "esp32_c049ef",
        }
        self.mqtt_publisher = mqtt_publisher

    def register_binding(self, host_id: str, device_id: str) -> None:
        self.binding_map[host_id] = device_id

    def handle_host_state(
        self, topic: str, payload_bytes: bytes, now: Optional[int] = None
    ) -> Optional[StateSyncPayload]:
        match = self.TOPIC_PATTERN.match(topic)
        if not match:
            logger.warning(f"Invalid host state topic: {topic}")
            return None

        src_host = match.group(1)

        try:
            raw_dict = json.loads(payload_bytes.decode("utf-8"))
            host_state = HostStatePayload.model_validate(raw_dict)
        except Exception as e:
            logger.error(f"Malformed host state payload: {e}")
            return None

        if host_state.host_id != src_host:
            logger.warning(f"Spoofing rejected: payload host_id '{host_state.host_id}' != topic '{src_host}'")
            return None

        target_device = self.binding_map.get(src_host)
        if not target_device:
            logger.info(f"No device bound to host '{src_host}', skipping state forwarding")
            return None

        # 组装 state_sync
        timestamp = now if now is not None else host_state.timestamp
        sync_payload = StateSyncPayload(
            type="state_sync",
            version=1,
            timestamp=timestamp,
            data=WorkModeData(work_mode=host_state.data.work_mode),
        )

        down_topic = f"sprite/device/{target_device}/down/state"
        if self.mqtt_publisher:
            self.mqtt_publisher(down_topic, sync_payload.model_dump_json(), 1, True)

        return sync_payload
