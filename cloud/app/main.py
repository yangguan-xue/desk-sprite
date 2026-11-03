"""FastAPI 核心主服务 (SPEC-001 第 3.2 节, SPEC-002 第 5 节, R01, R03)"""

import os
from typing import Optional
from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse

from cloud.app.api.v1.notify import router as notify_router
from cloud.app.api.v1.webhook import router as webhook_router
from cloud.app.services.auth import AuthService, load_auth_service_from_env
from cloud.app.services.event_store import EventStore
from cloud.app.services.idempotency import IdempotencyManager


def create_app(
    auth_service: Optional[AuthService] = None,
    idempotency_manager: Optional[IdempotencyManager] = None,
    event_store: Optional[EventStore] = None,
    webhook_secret: Optional[str] = None,
) -> FastAPI:
    app = FastAPI(title="Desk Sprite Cloud Hub", version="1.4.0")

    # R01: 默认仅从环境变量加载真实生产凭证，未配置时默认为空，绝不加载公开测试凭证
    app.state.auth_service = auth_service if auth_service is not None else load_auth_service_from_env()
    app.state.idempotency_manager = idempotency_manager or IdempotencyManager()
    app.state.event_store = event_store or EventStore()
    app.state.github_webhook_secret = webhook_secret if webhook_secret is not None else os.getenv("GITHUB_WEBHOOK_SECRET")

    # 全局参数校验错误映射为 SPEC-002 标准格式
    @app.exception_handler(RequestValidationError)
    async def validation_exception_handler(request: Request, exc: RequestValidationError):
        return JSONResponse(
            status_code=422,
            content={"code": 42201, "message": f"Validation error: {exc.errors()}", "data": None},
        )

    app.include_router(notify_router)
    app.include_router(webhook_router)

    @app.get("/health")
    async def health():
        return {"code": 0, "message": "healthy", "data": {"version": "1.4.0"}}

    return app


app = create_app()
