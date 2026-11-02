"""GitHub Webhook 接收与 HMAC 验签服务 (SPEC-001 第 3.2 节, TASK-004)

功能:
1. 基于 X-Hub-Signature-256 与 GITHUB_WEBHOOK_SECRET 进行常量时间比对 (hmac.compare_digest)
2. 验签失败绝不进入业务流水线
3. 将 GitHub 事件负载转换为 Desk Sprite 规范的 Alert 结构
"""

import hashlib
import hmac
import json
from typing import Any, Dict, Optional, Tuple

from shared.enums import PetAction, Priority, Sound


class WebhookVerificationError(Exception):
    """Webhook 验签失败异常"""
    pass


def verify_github_signature(raw_body: bytes, signature_header: Optional[str], secret: str) -> bool:
    """常量时间比对 GitHub Webhook HMAC SHA-256 签名"""
    if not signature_header or not secret:
        return False

    if not signature_header.startswith("sha256="):
        return False

    expected_sig = "sha256=" + hmac.new(secret.encode("utf-8"), raw_body, hashlib.sha256).hexdigest()
    return hmac.compare_digest(expected_sig, signature_header)


def convert_github_event_to_alert(
    event_type: str, payload: Dict[str, Any], default_device: str = "esp32_c049ef", default_host: str = "macbook_pro"
) -> Dict[str, Any]:
    """将 GitHub 事件转换为规范的 NotifyRequest 结构"""
    # 1. PR 事件
    if event_type == "pull_request":
        action = payload.get("action", "")
        pr = payload.get("pull_request", {})
        title = f"PR #{pr.get('number', '')} {action}"[:24]
        summary = pr.get("title", "GitHub Pull Request Update")[:48]
        url = pr.get("html_url")
        is_merged = pr.get("merged", False)

        priority = Priority.MEDIUM
        sound = Sound.DING
        pet_action = PetAction.CHEER if is_merged else PetAction.IDLE

        return {
            "target_device": default_device,
            "target_host": default_host,
            "priority": priority.value,
            "title": title,
            "summary": summary,
            "source": payload.get("repository", {}).get("full_name", "github")[:32],
            "sound": sound.value,
            "pet_action": pet_action.value,
            "duration_sec": 8,
            "url": url,
        }

    # 2. Workflow Run (CI/CD) 事件
    elif event_type == "workflow_run":
        wf = payload.get("workflow_run", {})
        conclusion = wf.get("conclusion", "")
        name = wf.get("name", "CI Workflow")

        is_success = conclusion == "success"
        priority = Priority.MEDIUM if is_success else Priority.HIGH
        sound = Sound.DING if is_success else Sound.ALERT_BEEP
        pet_action = PetAction.CHEER if is_success else PetAction.ALERT
        title = f"{name}: {conclusion}"[:24]
        summary = wf.get("display_title", name)[:48]
        url = wf.get("html_url")

        return {
            "target_device": default_device,
            "target_host": default_host,
            "priority": priority.value,
            "title": title,
            "summary": summary,
            "source": payload.get("repository", {}).get("full_name", "github_ci")[:32],
            "sound": sound.value,
            "pet_action": pet_action.value,
            "duration_sec": 8,
            "url": url,
        }

    # 3. 兜底通用处理
    else:
        repo_name = payload.get("repository", {}).get("full_name", "github")
        return {
            "target_device": default_device,
            "target_host": default_host,
            "priority": Priority.LOW.value,
            "title": f"GH: {event_type}"[:24],
            "summary": f"Received {event_type} event"[:48],
            "source": repo_name[:32],
            "sound": Sound.NONE.value,
            "pet_action": PetAction.IDLE.value,
            "duration_sec": 5,
            "url": None,
        }
