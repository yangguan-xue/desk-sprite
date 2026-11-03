"""MQTT 消息监听、路由与下发服务 (SPEC-001 第 3.2 节, SPEC-002 第 1 节, R03, R08)"""

import logging
import os
import ssl
import threading
import uuid
from typing import Callable, List, Optional

import paho.mqtt.client as mqtt

from cloud.app.services.action_router import ActionRouter
from cloud.app.services.lifecycle_monitor import LifecycleMonitor
from cloud.app.services.state_forwarder import StateForwarder

logger = logging.getLogger("mqtt_service")


class MQTTService:
    def __init__(
        self,
        action_router: Optional[ActionRouter] = None,
        state_forwarder: Optional[StateForwarder] = None,
        lifecycle_monitor: Optional[LifecycleMonitor] = None,
        host: str = "localhost",
        port: int = 1883,
        username: str = "cloud_admin",
        password: str = "password",
        tls_enabled: bool = False,
        ca_cert: Optional[str] = None,
    ):
        self.action_router = action_router
        self.state_forwarder = state_forwarder
        self.lifecycle_monitor = lifecycle_monitor
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.tls_enabled = tls_enabled or (port == 8883)
        self.ca_cert = ca_cert

        self._client: Optional[mqtt.Client] = None
        self._published_messages: List[dict] = []  # 记录最近发布的消息供测试断言与调试
        self._lock = threading.Lock()
        self._running = False
        self._connected = False

    @property
    def is_connected(self) -> bool:
        return self._connected

    def init_client(self) -> mqtt.Client:
        client_id = f"cloud_hub_{uuid.uuid4().hex[:8]}"
        client = mqtt.Client(
            client_id=client_id,
            clean_session=True,
            protocol=mqtt.MQTTv311,
        )

        if self.username:
            client.username_pw_set(self.username, self.password)

        if self.tls_enabled:
            if self.ca_cert and os.path.exists(self.ca_cert):
                client.tls_set(ca_certs=self.ca_cert)
            else:
                client.tls_set(cert_reqs=ssl.CERT_REQUIRED)

        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        client.on_message = self._on_message
        self._client = client
        return client

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self._connected = True
            logger.info("Connected to MQTT Broker (%s:%d) as %s", self.host, self.port, self.username)
            # 订阅设备动作、宿主状态与生命周期
            client.subscribe("sprite/device/+/up/action", qos=1)
            client.subscribe("sprite/host/+/up/state", qos=1)
            client.subscribe("sprite/host/+/up/status", qos=1)
            client.subscribe("sprite/device/+/up/status", qos=1)
        else:
            self._connected = False
            logger.error("Failed to connect to MQTT Broker, return code: %d", rc)

    def _on_disconnect(self, client, userdata, rc):
        self._connected = False
        if rc != 0:
            logger.warning("Unexpected disconnection from MQTT Broker, rc: %d", rc)

    def _on_message(self, client, userdata, msg):
        try:
            self.dispatch_message(msg.topic, msg.payload)
        except Exception as e:
            logger.error("Error dispatching message on %s: %s", msg.topic, e, exc_info=True)

    def start(self) -> None:
        """启动真实 MQTT 网络循环"""
        if self._client is None:
            self.init_client()
        self._running = True
        try:
            self._client.connect(self.host, self.port, keepalive=60)
            self._client.loop_start()
        except Exception as e:
            logger.warning("Could not connect to MQTT Broker on %s:%d: %s", self.host, self.port, e)

    def stop(self) -> None:
        """优雅关闭 MQTT 网络循环"""
        self._running = False
        if self._client:
            try:
                self._client.loop_stop()
                self._client.disconnect()
            except Exception:
                pass
            self._connected = False

    def publish(self, topic: str, payload: str, qos: int = 1, retain: bool = False) -> None:
        """向 MQTT Broker 发布消息"""
        record = {"topic": topic, "payload": payload, "qos": qos, "retain": retain}
        with self._lock:
            self._published_messages.append(record)

        if self._client and self._connected:
            self._client.publish(topic, payload=payload, qos=qos, retain=retain)
        else:
            logger.debug("MQTT client not connected, message recorded in-memory for %s", topic)

    def dispatch_message(self, topic: str, payload_bytes: bytes) -> None:
        """处理来自 Broker 的订阅消息 (R03 核心路由入口)"""
        if "/up/action" in topic and self.action_router:
            self.action_router.handle_device_action(topic, payload_bytes)
        elif "/up/state" in topic and self.state_forwarder:
            self.state_forwarder.handle_host_state(topic, payload_bytes)
        elif "/up/status" in topic and self.lifecycle_monitor:
            self.lifecycle_monitor.handle_status_message(topic, payload_bytes)
        else:
            logger.debug("Unhandled topic in dispatch_message: %s", topic)
