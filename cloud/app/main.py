"""FastAPI 核心主服务 (SPEC-001 第 3.2 节, SPEC-002 第 5 节)"""

import os
from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse

from cloud.app.api.v1.notify import router as notify_router
from cloud.app.api.v1.webhook import router as webhook_router
from cloud.app.services.auth import AuthService
from cloud.app.services.event_store import EventStore
from cloud.app.services.idempotency import IdempotencyManager


def create_app(
    auth_service: AuthService = None,
    idempotency_manager: IdempotencyManager = None,
    event_store: EventStore = None,
    webhook_secret: str = None,
) -> FastAPI:
    app = FastAPI(title="Desk Sprite Cloud Hub", version="1.4.0")

    app.state.auth_service = auth_service or AuthService()
    app.state.idempotency_manager = idempotency_manager or IdempotencyManager()
    app.state.event_store = event_store or EventStore()
    app.state.github_webhook_secret = webhook_secret or os.getenv("GITHUB_WEBHOOK_SECRET", "test_webhook_secret")

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
