"""MQTT 消息监听、路由与下发服务 (SPEC-001 第 3.2 节, SPEC-002 第 1 节)"""

import logging
from typing import Callable, Optional

from cloud.app.services.action_router import ActionRouter
from cloud.app.services.state_forwarder import StateForwarder

logger = logging.getLogger("mqtt_service")


class MQTTService:
    def __init__(
        self,
        action_router: Optional[ActionRouter] = None,
        state_forwarder: Optional[StateForwarder] = None,
        host: str = "localhost",
        port: int = 1883,
        username: str = "cloud_admin",
        password: str = "password",
    ):
        self.action_router = action_router
        self.state_forwarder = state_forwarder
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self._client = None
        self._published_messages = []  # 测试调试记录

    def publish(self, topic: str, payload: str, qos: int = 1, retain: bool = False) -> None:
        """向 MQTT Broker 发布消息"""
        self._published_messages.append({"topic": topic, "payload": payload, "qos": qos, "retain": retain})
        if self._client:
            self._client.publish(topic, payload=payload, qos=qos, retain=retain)

    def dispatch_message(self, topic: str, payload_bytes: bytes) -> None:
        """处理来自 Broker 的订阅消息"""
        if "/up/action" in topic and self.action_router:
            self.action_router.handle_device_action(topic, payload_bytes)
        elif "/up/state" in topic and self.state_forwarder:
            self.state_forwarder.handle_host_state(topic, payload_bytes)
        else:
            logger.debug(f"Unhandled topic: {topic}")
