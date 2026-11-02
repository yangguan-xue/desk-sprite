"""终端物理动作路由、归属校验与确定性命令派发流水线 (SPEC-001 3.2 节, SPEC-002 4 节, H01 闭环)"""

import hashlib
import json
import logging
import re
import threading
import time
from dataclasses import dataclass
from typing import Callable, Dict, Optional, Tuple

from cloud.app.schemas.device_action import DeviceActionPayload
from cloud.app.schemas.host_command import HostCommandParams, HostCommandPayload
from cloud.app.services.event_store import EventRecord, EventStore
from shared.enums import EventRecordState

logger = logging.getLogger("action_router")


@dataclass
class ActionRouteResult:
    status: str
    command: Optional[HostCommandPayload] = None
    target_host: Optional[str] = None
    audit_message: Optional[str] = None


class ActionRouter:
    """动作路由流水线"""

    TOPIC_PATTERN = re.compile(r"^sprite/device/([^/]+)/up/action$")

    def __init__(
        self,
        event_store: EventStore,
        mqtt_publisher: Optional[Callable[[str, str, int], None]] = None,
        time_provider: Optional[Callable[[], int]] = None,
    ):
        self.event_store = event_store
        self.mqtt_publisher = mqtt_publisher
        self._time_provider = time_provider or (lambda: int(time.time()))
        self._lock = threading.RLock()
        # 10 分钟 (600s) 命令去重缓存: command_id -> dispatched_at
        self._dispatch_cache: Dict[str, int] = {}

    def _now(self, now: Optional[int] = None) -> int:
        if now is not None:
            return now
        return self._time_provider()

    @staticmethod
    def compute_command_id(action_id: str) -> str:
        """确定性哈希计算: cmd_ + sha256(action_id)[:16] (SPEC-002 第 3.5 节)"""
        hash_hex = hashlib.sha256(action_id.encode("utf-8")).hexdigest()[:16]
        return f"cmd_{hash_hex}"

    def handle_device_action(
        self, topic: str, payload_bytes: bytes, now: Optional[int] = None
    ) -> ActionRouteResult:
        current_time = self._now(now)

        # 1. Topic 提取可信实体 ID
        match = self.TOPIC_PATTERN.match(topic)
        if not match:
            msg = f"Invalid topic pattern: {topic}"
            logger.warning(msg)
            return ActionRouteResult(status="invalid_topic", audit_message=msg)

        src_dev = match.group(1)

        # 2. 反序列化 Payload 并校验 Topic 一致性 (防伪造)
        try:
            raw_dict = json.loads(payload_bytes.decode("utf-8"))
            action_payload = DeviceActionPayload.model_validate(raw_dict)
        except Exception as e:
            msg = f"Malformed device action payload: {e}"
            logger.error(msg)
            return ActionRouteResult(status="malformed_payload", audit_message=msg)

        if action_payload.device_id != src_dev:
            msg = f"Security reject: payload device_id '{action_payload.device_id}' does not match topic src_dev '{src_dev}'"
            logger.warning(msg)
            return ActionRouteResult(status="spoofing_rejected", audit_message=msg)

        # 检查动作有效期 (10s)
        if current_time > action_payload.expire_at:
            msg = f"Action expired: current_time ({current_time}) > expire_at ({action_payload.expire_at})"
            logger.info(msg)
            return ActionRouteResult(status="action_expired", audit_message=msg)

        # 3. 检查是否有 active_alert_id
        active_alert_id = action_payload.data.active_alert_id
        if not active_alert_id:
            # 主屏操作，不生成 host_command
            return ActionRouteResult(status="main_screen_action")

        # 4. 在 EventStore 中检索对应记录
        record = self.event_store.get(active_alert_id, now=current_time)
        if not record:
            # 记录不存在或已超期淘汰 (>24h): 仅消除设备弹窗，安全拒绝派发命令 (H01 闭环)
            msg = f"Event record '{active_alert_id}' not found or expired; rejecting command generation"
            logger.info(msg)
            return ActionRouteResult(status="event_not_found_or_expired", audit_message=msg)

        # 5. 校验记录归属 (防跨设备冒名伪造)
        if record.target_device != src_dev:
            msg = f"Security reject: device '{src_dev}' attempted to claim alert '{active_alert_id}' owned by '{record.target_device}'"
            logger.warning(msg)
            return ActionRouteResult(status="ownership_rejected", audit_message=msg)

        # 6. 检查交互属性 (纯文本告警仅消除弹窗，不生成命令)
        if not record.is_interactive or not record.target_url:
            self.event_store.mark_consumed(active_alert_id, now=current_time)
            return ActionRouteResult(status="plain_alert_dismissed")

        # 7. 确定性计算 command_id
        cmd_id = self.compute_command_id(action_payload.action_id)

        # 8. 检查 10 分钟去重缓存
        with self._lock:
            # 清理超期缓存
            expired_cmds = [
                cid for cid, ts in self._dispatch_cache.items() if (current_time - ts) > 600
            ]
            for cid in expired_cmds:
                del self._dispatch_cache[cid]

            if cmd_id in self._dispatch_cache:
                msg = f"Command '{cmd_id}' already dispatched recently; skipping duplicate"
                logger.info(msg)
                return ActionRouteResult(status="duplicate_skipped", audit_message=msg)

            # 标记已派发
            self._dispatch_cache[cmd_id] = current_time

        # 9. 构造宿主下发指令
        expire_at = current_time + 60
        command = HostCommandPayload(
            type="host_command",
            version=1,
            command_id=cmd_id,
            source_action_id=action_payload.action_id,
            created_at=current_time,
            expire_at=expire_at,
            action="open_browser",
            params=HostCommandParams(url=record.target_url),
        )

        # 10. 派发下行 MQTT
        target_topic = f"sprite/host/{record.target_host}/down/command"
        if self.mqtt_publisher:
            self.mqtt_publisher(target_topic, command.model_dump_json(), 1)

        # 11. 标记事件已被消费
        self.event_store.mark_consumed(active_alert_id, now=current_time)

        return ActionRouteResult(
            status="command_dispatched",
            command=command,
            target_host=record.target_host,
        )
