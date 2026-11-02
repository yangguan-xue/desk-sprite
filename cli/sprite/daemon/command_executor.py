"""Mac 动作指令执行器与原子并发去重 (SPEC-004 第 3.2 节, TASK-011)

核心规则:
1. 内存容量 500 的 LRU 去重集合
2. 执行前原子占位 In-Flight: 已存在立即返回 ACK duplicate_ignored，严禁调用系统 open
3. 有效期拦截: now > expire_at 标记 expired
4. 严格 URL 白名单校验: http/https，无空格分号，长度 <= 512
5. 系统调用: 通过参数数组调用 /usr/bin/open，严禁 shell 拼接
"""

import collections
import logging
import re
import subprocess
import threading
import time
from typing import Callable, Optional

from cloud.app.schemas.command_ack import CommandAckPayload
from cloud.app.schemas.host_command import HostCommandPayload
from shared.enums import CommandAckStatus

logger = logging.getLogger("command_executor")


class CommandExecutor:
    URL_PATTERN = re.compile(r"^https?://[^\s;]+$")

    def __init__(
        self,
        host_id: str = "macbook_pro",
        system_opener: Optional[Callable[[str], bool]] = None,
        time_provider: Optional[Callable[[], int]] = None,
        lru_capacity: int = 500,
    ):
        self.host_id = host_id
        self.system_opener = system_opener or self._default_open
        self._time_provider = time_provider or (lambda: int(time.time()))
        self.lru_capacity = lru_capacity
        self._lock = threading.RLock()
        # LRU 缓存: command_id -> status
        self._lru_cache: collections.OrderedDict = collections.OrderedDict()

    def _now(self, now: Optional[int] = None) -> int:
        if now is not None:
            return now
        return self._time_provider()

    @staticmethod
    def _default_open(url: str) -> bool:
        """调用系统 /usr/bin/open，参数数组严禁 shell 拼接"""
        try:
            res = subprocess.run(["/usr/bin/open", url], capture_output=True, timeout=5.0)
            return res.returncode == 0
        except Exception as e:
            logger.error(f"Failed to execute /usr/bin/open: {e}")
            return False

    def execute(self, payload_dict: dict, now: Optional[int] = None) -> CommandAckPayload:
        current_time = self._now(now)

        # 1. 反序列化
        try:
            cmd = HostCommandPayload.model_validate(payload_dict)
        except Exception as e:
            logger.warning(f"Malformed host command: {e}")
            return CommandAckPayload(
                type="command_ack",
                version=1,
                host_id=self.host_id,
                command_id=str(payload_dict.get("command_id", "unknown"))[:32],
                status=CommandAckStatus.REJECTED,
                error_code=1,
                message=f"Malformed command: {e}"[:256],
                timestamp=current_time,
            )

        # 2. 有效期拦截
        if current_time > cmd.expire_at:
            logger.info(f"Command '{cmd.command_id}' expired ({current_time} > {cmd.expire_at})")
            return CommandAckPayload(
                type="command_ack",
                version=1,
                host_id=self.host_id,
                command_id=cmd.command_id,
                status=CommandAckStatus.EXPIRED,
                error_code=2,
                message="Command expired before execution",
                timestamp=current_time,
            )

        # 3. 结构化 URL 校验
        url = cmd.params.url
        if len(url) > 512 or not self.URL_PATTERN.match(url):
            logger.warning(f"Rejected insecure or malformed URL: {url}")
            return CommandAckPayload(
                type="command_ack",
                version=1,
                host_id=self.host_id,
                command_id=cmd.command_id,
                status=CommandAckStatus.REJECTED,
                error_code=3,
                message="Malformed or unsafe URL",
                timestamp=current_time,
            )

        # 4. 原子登记 In-Flight 与并发去重
        with self._lock:
            if cmd.command_id in self._lru_cache:
                logger.info(f"Command '{cmd.command_id}' already processed or in-flight; ignoring duplicate")
                return CommandAckPayload(
                    type="command_ack",
                    version=1,
                    host_id=self.host_id,
                    command_id=cmd.command_id,
                    status=CommandAckStatus.DUPLICATE_IGNORED,
                    error_code=0,
                    message="Duplicate command ignored",
                    timestamp=current_time,
                )

            # 淘汰超出容量的最老记录
            if len(self._lru_cache) >= self.lru_capacity:
                self._lru_cache.popitem(last=False)

            # 原子占位
            self._lru_cache[cmd.command_id] = "executing"

        # 5. 调用系统 open
        success = self.system_opener(url)

        with self._lock:
            self._lru_cache[cmd.command_id] = "success" if success else "failed"

        if success:
            return CommandAckPayload(
                type="command_ack",
                version=1,
                host_id=self.host_id,
                command_id=cmd.command_id,
                status=CommandAckStatus.SUCCESS,
                error_code=0,
                message="URL opened successfully",
                timestamp=current_time,
            )
        else:
            return CommandAckPayload(
                type="command_ack",
                version=1,
                host_id=self.host_id,
                command_id=cmd.command_id,
                status=CommandAckStatus.FAILED,
                error_code=4,
                message="System open invocation failed",
                timestamp=current_time,
            )
