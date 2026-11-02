"""sprite state 命令实现 (SPEC-004 第 2.3 节, TASK-010)

支持:
sprite state set <idle|coding|meeting|sleep> (进入 30 分钟临时锁定期)
sprite state auto (解除锁定，恢复自动探测)
"""

import json
import os
import sys
import time
from pathlib import Path
from typing import Optional

from cli.sprite.config import get_default_config_path, load_config
from shared.enums import WorkMode


def get_override_file_path() -> Path:
    config_path = get_default_config_path()
    return config_path.parent / "state_override.json"


def set_manual_override(mode_str: str, ttl_sec: int = 1800) -> dict:
    try:
        mode_enum = WorkMode(mode_str.lower())
    except ValueError:
        raise ValueError(f"Invalid mode '{mode_str}'. Must be one of: {[m.value for m in WorkMode]}")

    now = int(time.time())
    data = {
        "work_mode": mode_enum.value,
        "expires_at": now + ttl_sec,
        "set_at": now,
    }
    path = get_override_file_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    return data


def clear_manual_override() -> None:
    path = get_override_file_path()
    if path.exists():
        path.unlink()


def get_active_manual_override(now: Optional[int] = None) -> Optional[WorkMode]:
    path = get_override_file_path()
    if not path.exists():
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        current = now if now is not None else int(time.time())
        if current < data.get("expires_at", 0):
            return WorkMode(data["work_mode"])
        else:
            # 已超期
            path.unlink()
            return None
    except Exception:
        return None


def run_state_command(action: str, mode: Optional[str] = None) -> int:
    if action == "set":
        if not mode:
            sys.stderr.write("Error: Mode required for 'sprite state set <mode>'\n")
            return 1
        try:
            res = set_manual_override(mode)
            print(f"[OK] Mode manually set to '{res['work_mode']}' for 30 minutes (until {time.strftime('%H:%M:%S', time.localtime(res['expires_at']))})")
            return 0
        except ValueError as e:
            sys.stderr.write(f"Error: {e}\n")
            return 1
    elif action == "auto":
        clear_manual_override()
        print("[OK] Manual override cleared; restored auto-detection")
        return 0
    else:
        sys.stderr.write(f"Unknown action: {action}\n")
        return 1
