"""守护进程 MQTT Worker 客户端 (SPEC-004 第 3.2 节, TASK-011)

核心契约:
1. clean_session = True 干净会话，重启后不重放离线历史指令
2. 订阅 sprite/host/{host_id}/down/command
3. LWT 遗嘱消息与在线状态声明
4. 收到指令后执行并向 sprite/host/{host_id}/up/ack 上报结果回执
"""

import json
import logging
import time
from typing import Optional

import paho.mqtt.client as mqtt

from cli.sprite.config import SpriteConfig
from cli.sprite.daemon.command_executor import CommandExecutor
from cloud.app.schemas.status import StatusLifecyclePayload
from shared.enums import LifecycleStatus

logger = logging.getLogger("mqtt_worker")


class DaemonMQTTWorker:
    def __init__(self, config: SpriteConfig, executor: CommandExecutor):
        self.config = config
        self.executor = executor
        self.client_id = f"cid_host_{config.host_id}"
        self.down_command_topic = f"sprite/host/{config.host_id}/down/command"
        self.up_ack_topic = f"sprite/host/{config.host_id}/up/ack"
        self.status_topic = f"sprite/host/{config.host_id}/up/status"
        self.state_topic = f"sprite/host/{config.host_id}/up/state"

        # clean_session = True (SPEC-004 第 3.2 节)
        self.client = mqtt.Client(
            client_id=self.client_id,
            clean_session=True,
            protocol=mqtt.MQTTv311,
        )

        if config.mqtt_username:
            self.client.username_pw_set(config.mqtt_username, config.mqtt_password)

        self._setup_lwt()
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self._connected = False

    def _setup_lwt(self) -> None:
        lwt_payload = StatusLifecyclePayload(
            type="status_lifecycle",
            version=1,
            status=LifecycleStatus.OFFLINE,
            reason="connection_lost",
            timestamp=int(time.time()),
        )
        self.client.will_set(
            topic=self.status_topic,
            payload=lwt_payload.model_dump_json(),
            qos=1,
            retain=True,
        )

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            logger.info(f"Connected to MQTT Broker at {self.config.mqtt_broker}:{self.config.mqtt_port}")
            self._connected = True
            # 上报在线声明
            online_payload = StatusLifecyclePayload(
                type="status_lifecycle",
                version=1,
                status=LifecycleStatus.ONLINE,
                client_version="1.4.0",
                timestamp=int(time.time()),
            )
            self.client.publish(self.status_topic, online_payload.model_dump_json(), qos=1, retain=True)

            # 订阅下行命令
            self.client.subscribe(self.down_command_topic, qos=1)
            logger.info(f"Subscribed to {self.down_command_topic}")
        else:
            logger.error(f"MQTT connect failed with rc={rc}")

    def _on_message(self, client, userdata, msg):
        try:
            raw_dict = json.loads(msg.payload.decode("utf-8"))
            ack = self.executor.execute(raw_dict)
            # 上报执行回执
            self.client.publish(self.up_ack_topic, ack.model_dump_json(), qos=1)
            logger.info(f"Reported command ACK for {ack.command_id}: status={ack.status}")
        except Exception as e:
            logger.error(f"Failed to process message on {msg.topic}: {e}")

    def publish_work_mode(self, mode_str: str) -> None:
        """Mac 状态感知更新上报 (sprite/host/{host_id}/up/state)"""
        now = int(time.time())
        payload = {
            "type": "host_state_update",
            "version": 1,
            "host_id": self.config.host_id,
            "timestamp": now,
            "data": {
                "work_mode": mode_str,
            },
        }
        self.client.publish(self.state_topic, json.dumps(payload), qos=1, retain=True)

    def start(self) -> None:
        self.client.connect_async(self.config.mqtt_broker, self.config.mqtt_port, 60)
        self.client.loop_start()

    def stop(self) -> None:
        if self._connected:
            # 发送优雅退出下线消息
            offline_payload = StatusLifecyclePayload(
                type="status_lifecycle",
                version=1,
                status=LifecycleStatus.OFFLINE,
                reason="graceful_shutdown",
                timestamp=int(time.time()),
            )
            self.client.publish(self.status_topic, offline_payload.model_dump_json(), qos=1, retain=True)

        self.client.loop_stop()
        self.client.disconnect()
        logger.info("DaemonMQTTWorker stopped")
