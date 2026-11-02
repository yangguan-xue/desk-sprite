"""sprite notify 命令实现 (SPEC-004 第 2.1 节, TASK-008)

退出码规范:
0: 通知已成功发送并被云端接收
1: 本地配置缺失或参数非法 (如非法枚举)
2: 网络不可达或云端鉴权失败 (401 / 403)
"""

import sys
from typing import Optional

import httpx

from cli.sprite.config import SpriteConfig, load_config
from shared.enums import PetAction, Priority, Sound


def run_notify(
    title: str,
    summary: Optional[str] = None,
    level: str = "medium",
    sound: str = "ding",
    mood: str = "cheer",
    url: Optional[str] = None,
    device: Optional[str] = None,
    config: Optional[SpriteConfig] = None,
    timeout: float = 5.0,
) -> int:
    # 1. 验证枚举合法性 (SPEC-002 第 2 节, 严禁使用 info 等非法值)
    try:
        priority_enum = Priority(level.lower())
    except ValueError:
        sys.stderr.write(f"Error: Invalid level '{level}'. Must be one of: {[p.value for p in Priority]}\n")
        return 1

    try:
        sound_enum = Sound(sound.lower())
    except ValueError:
        sys.stderr.write(f"Error: Invalid sound '{sound}'. Must be one of: {[s.value for s in Sound]}\n")
        return 1

    try:
        mood_enum = PetAction(mood.lower())
    except ValueError:
        sys.stderr.write(f"Error: Invalid mood '{mood}'. Must be one of: {[m.value for m in PetAction]}\n")
        return 1

    # 2. 检查标题与摘要
    if not title or len(title) > 24:
        sys.stderr.write(f"Error: Title must be between 1 and 24 characters (got {len(title) if title else 0})\n")
        return 1

    summary_str = summary or title
    if len(summary_str) > 48:
        sys.stderr.write(f"Error: Summary must be at most 48 characters (got {len(summary_str)})\n")
        return 1

    # 3. 加载配置
    try:
        cfg = config or load_config()
    except Exception as e:
        sys.stderr.write(f"Error: Failed to load config: {e}\n")
        return 1

    target_device = device or cfg.target_device_id
    if not target_device:
        sys.stderr.write("Error: target_device not specified and no default in config\n")
        return 1

    # 4. 构造请求体
    payload = {
        "target_device": target_device,
        "target_host": cfg.host_id,
        "priority": priority_enum.value,
        "title": title,
        "summary": summary_str,
        "source": "sprite-cli",
        "sound": sound_enum.value,
        "pet_action": mood_enum.value,
        "duration_sec": 8,
        "url": url,
    }

    url_endpoint = f"{cfg.cloud_endpoint.rstrip('/')}/api/v1/notify"
    headers = {
        "Authorization": f"Bearer {cfg.api_key}",
        "Content-Type": "application/json",
    }

    # 5. 发送 HTTP 请求
    try:
        with httpx.Client(timeout=timeout) as client:
            resp = client.post(url_endpoint, json=payload, headers=headers)

        if resp.status_code == 200:
            return 0
        elif resp.status_code in (401, 403):
            sys.stderr.write(f"Auth error ({resp.status_code}): {resp.text}\n")
            return 2
        else:
            sys.stderr.write(f"Cloud error ({resp.status_code}): {resp.text}\n")
            return 1
    except (httpx.ConnectError, httpx.TimeoutException, httpx.NetworkError) as e:
        sys.stderr.write(f"Network error: {e}\n")
        return 2
    except Exception as e:
        sys.stderr.write(f"Unexpected error: {e}\n")
        return 1
