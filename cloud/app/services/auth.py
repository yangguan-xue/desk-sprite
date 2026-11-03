"""API Key 鉴权与越权防护服务 (SPEC-002 第 5 节, R01, R02)"""

from dataclasses import dataclass, field
import json
import os
from typing import Dict, List, Optional, Set


class AuthError(Exception):
    def __init__(self, http_status: int, code: int, message: str):
        super().__init__(message)
        self.http_status = http_status
        self.code = code
        self.message = message


@dataclass
class KeyProfile:
    owner_id: str
    allowed_devices: List[str]
    default_host: str
    allowed_hosts: List[str] = field(default_factory=list)

    def __post_init__(self):
        if not self.allowed_hosts and self.default_host:
            self.allowed_hosts = [self.default_host]


class AuthService:
    def __init__(self, keys: Optional[Dict[str, KeyProfile]] = None):
        # 默认不内置任何测试或公开凭证 (R01 安全基准)
        self.keys: Dict[str, KeyProfile] = {}
        self.system_devices: Set[str] = set()

        if keys:
            for k, p in keys.items():
                self.register_key(k, p)

    def register_key(self, api_key: str, profile: KeyProfile) -> None:
        self.keys[api_key] = profile
        for dev in profile.allowed_devices:
            self.system_devices.add(dev)

    def verify_request(
        self,
        auth_header: Optional[str],
        target_device: str,
        target_host: Optional[str] = None,
    ) -> KeyProfile:
        """验证请求合法性，执行 401/403/404 判定与跨宿主越权校验 (R02)"""
        if not auth_header or not auth_header.startswith("Bearer "):
            raise AuthError(401, 40101, "Invalid API token or signature")

        token = auth_header[len("Bearer "):].strip()
        profile = self.keys.get(token)
        if not profile:
            raise AuthError(401, 40101, "Invalid API token or signature")

        # 检查目标设备是否存在 (404)
        if target_device not in self.system_devices:
            raise AuthError(404, 40401, "Device not found")

        # 检查是否在当前 Key 授权的设备列表中 (403 越权防护)
        if target_device not in profile.allowed_devices:
            raise AuthError(403, 40301, "Forbidden: Device not authorized")

        # 检查目标宿主 (若显式指定) 是否在当前 Key 授权的宿主列表中 (R02 跨宿主越权防护)
        if target_host and target_host not in profile.allowed_hosts:
            raise AuthError(403, 40301, f"Forbidden: Host '{target_host}' not authorized for caller")

        return profile


def load_auth_service_from_env() -> AuthService:
    """从环境变量加载真实生产凭证 (SPRITE_API_KEYS)，未配置时返回空注册表"""
    auth = AuthService()
    raw = os.getenv("SPRITE_API_KEYS")
    if raw:
        try:
            data = json.loads(raw)
            for k, v in data.items():
                auth.register_key(
                    k,
                    KeyProfile(
                        owner_id=v["owner_id"],
                        allowed_devices=v["allowed_devices"],
                        default_host=v.get("default_host", ""),
                        allowed_hosts=v.get("allowed_hosts", [v.get("default_host")] if v.get("default_host") else []),
                    ),
                )
        except Exception:
            pass
    return auth


def create_test_auth_service() -> AuthService:
    """仅供测试套件显式注入使用的测试注册表 (不进入默认应用)"""
    auth = AuthService()
    auth.register_key(
        "spr_live_test_key",
        KeyProfile(
            owner_id="owner_dev",
            allowed_devices=["esp32_c049ef"],
            default_host="macbook_pro",
            allowed_hosts=["macbook_pro"],
        ),
    )
    auth.register_key(
        "spr_live_guest_key",
        KeyProfile(
            owner_id="owner_guest",
            allowed_devices=["esp32_guest"],
            default_host="macbook_guest",
            allowed_hosts=["macbook_guest"],
        ),
    )
    return auth
