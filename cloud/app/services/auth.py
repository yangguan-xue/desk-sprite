"""API Key 鉴权与越权防护服务 (SPEC-002 第 5 节)"""

from dataclasses import dataclass
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


class AuthService:
    def __init__(self):
        # 预制默认测试与系统注册表 (可从环境变量或配置扩展)
        self.keys: Dict[str, KeyProfile] = {
            "spr_live_test_key": KeyProfile(
                owner_id="owner_dev",
                allowed_devices=["esp32_c049ef"],
                default_host="macbook_pro",
            ),
            "spr_live_guest_key": KeyProfile(
                owner_id="owner_guest",
                allowed_devices=["esp32_guest"],
                default_host="macbook_guest",
            ),
        }
        self.system_devices: Set[str] = {"esp32_c049ef", "esp32_guest"}

    def register_key(self, api_key: str, profile: KeyProfile) -> None:
        self.keys[api_key] = profile
        for dev in profile.allowed_devices:
            self.system_devices.add(dev)

    def verify_request(self, auth_header: Optional[str], target_device: str) -> KeyProfile:
        """验证请求合法性，执行 401/403/404 判定"""
        if not auth_header or not auth_header.startswith("Bearer "):
            raise AuthError(401, 40101, "Invalid API token or signature")

        token = auth_header[len("Bearer "):].strip()
        profile = self.keys.get(token)
        if not profile:
            raise AuthError(401, 40101, "Invalid API token or signature")

        # 检查目标设备是否存在
        if target_device not in self.system_devices:
            raise AuthError(404, 40401, "Device not found")

        # 检查是否在当前 Key 授权的设备列表中 (403 越权防护)
        if target_device not in profile.allowed_devices:
            raise AuthError(403, 40301, "Forbidden: Device not authorized")

        return profile
