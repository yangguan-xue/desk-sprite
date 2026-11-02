"""Mac 客户端配置模型与安全权限校验 (SPEC-004 第 4.2 节, TASK-007)

规则:
1. 默认路径 ~/.sprite/config.json (支持环境变量 SPRITE_CONFIG_PATH 覆盖)
2. 权限校验: 目录必须为 0700，文件必须为 0600；权限过宽时自动修复为 0600 并记录告警
3. 字段完整性校验与默认模板生成
"""

import json
import logging
import os
import stat
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Dict, Optional

logger = logging.getLogger("sprite_config")


@dataclass
class StateMonitorConfig:
    enabled: bool = True
    idle_timeout_sec: int = 300
    debounce_sec: int = 15
    manual_override_ttl_sec: int = 1800


@dataclass
class SpriteConfig:
    cloud_endpoint: str = "http://localhost:8000"
    mqtt_broker: str = "localhost"
    mqtt_port: int = 1883
    host_id: str = "macbook_pro"
    target_device_id: str = "esp32_c049ef"
    api_key: str = "spr_live_test_key"
    mqtt_username: str = "host_macbook_pro"
    mqtt_password: str = "spr_mqtt_secret_test"
    state_monitor: StateMonitorConfig = field(default_factory=StateMonitorConfig)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "SpriteConfig":
        monitor_data = data.get("state_monitor", {})
        monitor_cfg = StateMonitorConfig(**monitor_data)
        data_copy = {k: v for k, v in data.items() if k != "state_monitor"}
        return cls(**data_copy, state_monitor=monitor_cfg)

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        return d


def get_default_config_path() -> Path:
    env_path = os.getenv("SPRITE_CONFIG_PATH")
    if env_path:
        return Path(env_path).expanduser().resolve()
    return Path.home() / ".sprite" / "config.json"


def ensure_secure_permissions(path: Path) -> None:
    """确保目录 0700 与文件 0600 安全权限 (SPEC-004 第 4.2 节)"""
    parent = path.parent
    if not parent.exists():
        parent.mkdir(parents=True, mode=0o700)
    else:
        current_dir_mode = stat.S_IMODE(parent.stat().st_mode)
        if current_dir_mode != 0o700:
            logger.warning(f"Fixing insecure directory permissions {oct(current_dir_mode)} -> 0700 on {parent}")
            parent.chmod(0o700)

    if path.exists():
        current_file_mode = stat.S_IMODE(path.stat().st_mode)
        if current_file_mode != 0o600:
            logger.warning(f"Fixing insecure file permissions {oct(current_file_mode)} -> 0600 on {path}")
            path.chmod(0o600)


def load_config(config_path: Optional[Path] = None) -> SpriteConfig:
    path = config_path or get_default_config_path()
    if not path.exists():
        # 如果文件不存在，生成默认模板文件
        cfg = SpriteConfig()
        save_config(cfg, path)
        return cfg

    ensure_secure_permissions(path)

    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    return SpriteConfig.from_dict(data)


def save_config(config: SpriteConfig, config_path: Optional[Path] = None) -> None:
    path = config_path or get_default_config_path()
    ensure_secure_permissions(path)

    with open(path, "w", encoding="utf-8") as f:
        json.dump(config.to_dict(), f, indent=2, ensure_ascii=False)

    ensure_secure_permissions(path)
