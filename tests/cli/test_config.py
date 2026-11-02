"""Mac 配置加载与权限安全测试 (TASK-007)"""

import os
import stat
import tempfile
from pathlib import Path

import pytest

from cli.sprite.config import SpriteConfig, load_config, save_config


def test_config_generation_and_permissions():
    with tempfile.TemporaryDirectory() as tmpdir:
        config_path = Path(tmpdir) / ".sprite" / "config.json"

        # 加载不存在的配置文件，验证自动生成
        cfg = load_config(config_path)
        assert cfg.host_id == "macbook_pro"
        assert cfg.target_device_id == "esp32_c049ef"
        assert config_path.exists()

        # 验证文件权限严格为 0600
        file_mode = stat.S_IMODE(config_path.stat().st_mode)
        assert file_mode == 0o600

        # 验证目录权限严格为 0700
        dir_mode = stat.S_IMODE(config_path.parent.stat().st_mode)
        assert dir_mode == 0o700


def test_insecure_permissions_auto_fix():
    with tempfile.TemporaryDirectory() as tmpdir:
        config_path = Path(tmpdir) / "config.json"
        cfg = SpriteConfig(api_key="spr_secret")
        save_config(cfg, config_path)

        # 故意将权限修改为宽松的 0644
        config_path.chmod(0o644)
        assert stat.S_IMODE(config_path.stat().st_mode) == 0o644

        # 重新加载，验证被自动修复为 0600
        loaded = load_config(config_path)
        assert loaded.api_key == "spr_secret"
        assert stat.S_IMODE(config_path.stat().st_mode) == 0o600
