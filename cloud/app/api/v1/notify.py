"""发送通知接口实现 POST /api/v1/notify (SPEC-002 第 5.2 节)"""

import time
import uuid
from typing import Optional

from fastapi import APIRouter, Header, Request
from fastapi.responses import JSONResponse

from cloud.app.schemas.alert import AlertData, AlertPayload, InteractiveAction
from cloud.app.schemas.notify_request import NotifyRequest
from cloud.app.services.auth import AuthError
from cloud.app.services.event_store import EventRecord
from cloud.app.services.idempotency import IdempotencyConflictError
from shared.enums import Priority

router = APIRouter(prefix="/api/v1", tags=["notify"])


@router.post("/notify")
async def notify(
    request: Request,
    authorization: Optional[str] = Header(None),
    idempotency_key: Optional[str] = Header(None, alias="Idempotency-Key"),
):
    # 读取原始字节体用于幂等哈希比对
    raw_body = await request.body()
    auth_service = request.app.state.auth_service
    idempotency_manager = request.app.state.idempotency_manager
    event_store = request.app.state.event_store
    mqtt_service = getattr(request.app.state, "mqtt_service", None)

    # 1. 基础 JSON 解析
    try:
        import json
        body_dict = json.loads(raw_body.decode("utf-8"))
    except Exception as e:
        return JSONResponse(
            status_code=400,
            content={"code": 40001, "message": f"Validation error: invalid JSON - {e}", "data": None},
        )

    # 2. Schema 校验
    try:
        req_data = NotifyRequest.model_validate(body_dict)
    except Exception as e:
        # SPEC-002: 400 Bad Request (必填缺失、字符超长)，422 (枚举值非法、URL格式错误)
        from pydantic import ValidationError
        err_str = str(e)
        is_length_or_missing = False
        if isinstance(e, ValidationError):
            for err in e.errors():
                err_type = err.get("type", "")
                if "too_long" in err_type or "too_short" in err_type or "missing" in err_type:
                    is_length_or_missing = True
                    break

        if is_length_or_missing or "at most" in err_str or "at least" in err_str:
            return JSONResponse(
                status_code=400,
                content={"code": 40001, "message": f"Validation error: {err_str}", "data": None},
            )
        return JSONResponse(
            status_code=422,
            content={"code": 42201, "message": f"Invalid enum or malformed URL: {err_str}", "data": None},
        )

    # 3. API Key 鉴权与越权拦截 (401 / 403 / 404)
    try:
        profile = auth_service.verify_request(authorization, req_data.target_device)
    except AuthError as ae:
        return JSONResponse(
            status_code=ae.http_status,
            content={"code": ae.code, "message": ae.message, "data": None},
        )

    # 4. 幂等性处理 (5 分钟缓存)
    if idempotency_key:
        try:
            cached_resp = idempotency_manager.check_or_reserve(
                owner_id=profile.owner_id,
                idempotency_key=idempotency_key,
                body_bytes=raw_body,
            )
            if cached_resp is not None:
                # 相同 Key + 相同 Body: 直接返回历史响应，杜绝重复派发 MQTT
                return JSONResponse(status_code=200, content=cached_resp)
        except IdempotencyConflictError as ice:
            # 相同 Key + 不同 Body: 409 Conflict
            return JSONResponse(
                status_code=409,
                content={"code": 40901, "message": str(ice), "data": None},
            )

    # 5. 生成唯一事件与存入 EventStore
    now = int(time.time())
    event_id = f"evt_{uuid.uuid4().hex[:16]}"
    target_host = req_data.target_host or profile.default_host
    is_interactive = bool(req_data.url)

    record = EventRecord(
        event_id=event_id,
        target_device=req_data.target_device,
        target_host=target_host,
        target_url=req_data.url,
        is_interactive=is_interactive,
        created_at=now,
        priority=req_data.priority,
    )
    event_store.save(record)

    # 6. 下发 MQTT Alert
    expire_at = now + 300
    interactive_obj = None
    if is_interactive and req_data.url:
        interactive_obj = InteractiveAction(
            tap_action="open_url",
            target_url=req_data.url,
            target_host=target_host,
        )

    alert_payload = AlertPayload(
        type="alert",
        version=1,
        event_id=event_id,
        timestamp=now,
        expire_at=expire_at,
        data=AlertData(
            category="notify",
            priority=req_data.priority,
            title=req_data.title,
            summary=req_data.summary,
            source=req_data.source,
            sound=req_data.sound,
            pet_action=req_data.pet_action,
            duration_sec=req_data.duration_sec,
            interactive=interactive_obj,
        ),
    )

    if mqtt_service:
        topic = f"sprite/device/{req_data.target_device}/down/alert"
        mqtt_service.publish(topic, alert_payload.model_dump_json(), qos=1)

    # 7. 组装标准响应
    resp_content = {
        "code": 0,
        "message": "success",
        "data": {
            "event_id": event_id,
            "target_device": req_data.target_device,
        },
    }

    if idempotency_key:
        idempotency_manager.save_response(profile.owner_id, idempotency_key, resp_content)

    return JSONResponse(status_code=200, content=resp_content)
