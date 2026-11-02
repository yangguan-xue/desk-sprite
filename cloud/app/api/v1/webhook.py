"""GitHub Webhook 接口实现 POST /api/v1/webhook/github (TASK-004)"""

import json
import time
import uuid
from typing import Optional

from fastapi import APIRouter, Header, Request
from fastapi.responses import JSONResponse

from cloud.app.schemas.alert import AlertData, AlertPayload, InteractiveAction
from cloud.app.services.event_store import EventRecord
from cloud.app.services.webhook_verifier import (
    convert_github_event_to_alert,
    verify_github_signature,
)
from shared.enums import PetAction, Priority, Sound

router = APIRouter(prefix="/api/v1/webhook", tags=["webhook"])


@router.post("/github")
async def github_webhook(
    request: Request,
    x_hub_signature_256: Optional[str] = Header(None, alias="X-Hub-Signature-256"),
    x_github_event: Optional[str] = Header("pull_request", alias="X-GitHub-Event"),
):
    raw_body = await request.body()
    webhook_secret = getattr(request.app.state, "github_webhook_secret", "test_webhook_secret")
    event_store = request.app.state.event_store
    mqtt_service = getattr(request.app.state, "mqtt_service", None)

    # 1. 常量时间 HMAC SHA-256 验签 (401 拦截)
    if not verify_github_signature(raw_body, x_hub_signature_256, webhook_secret):
        return JSONResponse(
            status_code=401,
            content={"code": 40101, "message": "Invalid API token or signature", "data": None},
        )

    # 2. 解析 JSON Body
    try:
        body_dict = json.loads(raw_body.decode("utf-8"))
    except Exception as e:
        return JSONResponse(
            status_code=400,
            content={"code": 40001, "message": f"Validation error: invalid JSON - {e}", "data": None},
        )

    # 3. 转换为标准 Alert 结构
    alert_info = convert_github_event_to_alert(
        event_type=x_github_event or "pull_request",
        payload=body_dict,
    )

    now = int(time.time())
    event_id = f"evt_gh_{uuid.uuid4().hex[:12]}"
    target_device = alert_info["target_device"]
    target_host = alert_info["target_host"]
    url = alert_info.get("url")
    is_interactive = bool(url)

    # 4. 存入 EventStore
    record = EventRecord(
        event_id=event_id,
        target_device=target_device,
        target_host=target_host,
        target_url=url,
        is_interactive=is_interactive,
        created_at=now,
        priority=Priority(alert_info["priority"]),
    )
    event_store.save(record)

    # 5. 下发 MQTT
    expire_at = now + 300
    interactive_obj = None
    if is_interactive and url:
        interactive_obj = InteractiveAction(
            tap_action="open_url",
            target_url=url,
            target_host=target_host,
        )

    alert_payload = AlertPayload(
        type="alert",
        version=1,
        event_id=event_id,
        timestamp=now,
        expire_at=expire_at,
        data=AlertData(
            category="github",
            priority=Priority(alert_info["priority"]),
            title=alert_info["title"],
            summary=alert_info["summary"],
            source=alert_info.get("source"),
            sound=Sound(alert_info["sound"]),
            pet_action=PetAction(alert_info["pet_action"]),
            duration_sec=alert_info["duration_sec"],
            interactive=interactive_obj,
        ),
    )

    if mqtt_service:
        topic = f"sprite/device/{target_device}/down/alert"
        mqtt_service.publish(topic, alert_payload.model_dump_json(), qos=1)

    return JSONResponse(
        status_code=200,
        content={
            "code": 0,
            "message": "success",
            "data": {
                "event_id": event_id,
                "target_device": target_device,
            },
        },
    )
