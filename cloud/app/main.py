"""FastAPI 核心主服务 (SPEC-001 第 3.2 节, SPEC-002 第 5 节, R01, R03)"""

import asyncio
from contextlib import asynccontextmanager
import logging
import os
from typing import Optional
from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse

from cloud.app.api.v1.notify import router as notify_router
from cloud.app.api.v1.webhook import router as webhook_router
from cloud.app.services.action_router import ActionRouter
from cloud.app.services.auth import AuthService, load_auth_service_from_env
from cloud.app.services.event_store import EventStore
from cloud.app.services.idempotency import IdempotencyManager
from cloud.app.services.lifecycle_monitor import LifecycleMonitor
from cloud.app.services.metrics_engine import MetricsEngine
from cloud.app.services.mqtt_service import MQTTService
from cloud.app.services.state_forwarder import StateForwarder

logger = logging.getLogger("cloud_main")


@asynccontextmanager
async def lifespan(app: FastAPI):
    """应用生命周期管理与后台 MQTT / 监控服务装配 (R03)"""
    mqtt_service: MQTTService = app.state.mqtt_service
    lifecycle_monitor: LifecycleMonitor = app.state.lifecycle_monitor
    metrics_engine: MetricsEngine = app.state.metrics_engine

    # 启动真实 MQTT 网络连接 (默认开启自动连接与后台重连守护，RR01)
    auto_connect = os.getenv("MQTT_AUTO_CONNECT", "true").lower() in ("true", "1", "yes")
    background_tasks = []

    if auto_connect:
        logger.info("Auto-connecting to MQTT Broker...")
        mqtt_service.start()

        # 启动周期性下发看板与离线检测后台任务
        async def metrics_loop():
            while True:
                try:
                    await asyncio.sleep(60.0)
                    metrics_engine.push_metrics("esp32_c049ef")
                except asyncio.CancelledError:
                    break
                except Exception as e:
                    logger.error(f"Error in metrics loop: {e}")

        async def lifecycle_loop():
            while True:
                try:
                    await asyncio.sleep(10.0)
                    lifecycle_monitor.check_offline_timeouts()
                except asyncio.CancelledError:
                    break
                except Exception as e:
                    logger.error(f"Error in lifecycle loop: {e}")

        async def cleanup_loop():
            while True:
                try:
                    await asyncio.sleep(60.0)
                    app.state.event_store.cleanup_expired()
                    app.state.idempotency_manager.cleanup_expired()
                except asyncio.CancelledError:
                    break
                except Exception as e:
                    logger.error(f"Error in cleanup loop: {e}")

        background_tasks.append(asyncio.create_task(metrics_loop()))
        background_tasks.append(asyncio.create_task(lifecycle_loop()))
        background_tasks.append(asyncio.create_task(cleanup_loop()))

    yield

    # 关闭后台任务与 MQTT 连接
    for task in background_tasks:
        task.cancel()
    mqtt_service.stop()


def create_app(
    auth_service: Optional[AuthService] = None,
    idempotency_manager: Optional[IdempotencyManager] = None,
    event_store: Optional[EventStore] = None,
    webhook_secret: Optional[str] = None,
    mqtt_service: Optional[MQTTService] = None,
    action_router: Optional[ActionRouter] = None,
    state_forwarder: Optional[StateForwarder] = None,
    lifecycle_monitor: Optional[LifecycleMonitor] = None,
    metrics_engine: Optional[MetricsEngine] = None,
) -> FastAPI:
    app = FastAPI(title="Desk Sprite Cloud Hub", version="1.4.0", lifespan=lifespan)

    # 1. 基础状态存储与鉴权
    app.state.auth_service = auth_service if auth_service is not None else load_auth_service_from_env()
    app.state.idempotency_manager = idempotency_manager or IdempotencyManager()
    app.state.event_store = event_store or EventStore()
    app.state.github_webhook_secret = webhook_secret if webhook_secret is not None else os.getenv("GITHUB_WEBHOOK_SECRET")

    # 2. 组装动作路由、状态转发、度量与生命周期监视器 (R03 闭环)
    resolved_event_store = app.state.event_store

    resolved_mqtt = mqtt_service or MQTTService(
        host=os.getenv("MQTT_HOST", "localhost"),
        port=int(os.getenv("MQTT_PORT", "1883")),
        username=os.getenv("MQTT_USERNAME", "cloud_admin"),
        password=os.getenv("MQTT_PASSWORD", "password"),
        tls_enabled=os.getenv("MQTT_TLS_ENABLED", "false").lower() in ("true", "1", "yes"),
        ca_cert=os.getenv("MQTT_CA_CERT"),
    )

    resolved_action_router = action_router or ActionRouter(
        event_store=resolved_event_store,
        mqtt_publisher=resolved_mqtt.publish,
    )

    resolved_state_forwarder = state_forwarder or StateForwarder(
        mqtt_publisher=resolved_mqtt.publish,
    )

    resolved_lifecycle_monitor = lifecycle_monitor or LifecycleMonitor(
        mqtt_publisher=resolved_mqtt.publish,
    )

    resolved_metrics_engine = metrics_engine or MetricsEngine(
        mqtt_publisher=resolved_mqtt.publish,
    )

    # 交叉绑定至 MQTTService
    resolved_mqtt.action_router = resolved_action_router
    resolved_mqtt.state_forwarder = resolved_state_forwarder
    resolved_mqtt.lifecycle_monitor = resolved_lifecycle_monitor

    app.state.mqtt_service = resolved_mqtt
    app.state.action_router = resolved_action_router
    app.state.state_forwarder = resolved_state_forwarder
    app.state.lifecycle_monitor = resolved_lifecycle_monitor
    app.state.metrics_engine = resolved_metrics_engine

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
        return {
            "code": 0,
            "message": "healthy",
            "data": {
                "version": "1.4.0",
                "mqtt_connected": app.state.mqtt_service.is_connected,
            },
        }

    return app


app = create_app()
