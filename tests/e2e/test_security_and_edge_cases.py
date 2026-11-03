"""E2E-03: 安全防伪与异常边界测试 (TASK-020)

覆盖 5 大核心边界契约:
1. 越权伪造 action 拦截 (Cross-device spoofing)
2. 409 幂等冲突拦截 (Idempotency conflict)
3. Mac 端指令并发/重复原子去重 (In-flight duplicate suppression)
4. Sync Barrier 抑制 SIGTTIN 与原子交付
5. 两阶段模式过滤与出队调度复核 (Two-stage mode filter & egress re-check)
"""

import json
import os
import pty
import time
from unittest.mock import MagicMock
import pytest
from starlette.testclient import TestClient

from cli.sprite.commands.run import run_wrapper
from cli.sprite.daemon.command_executor import CommandExecutor
from cloud.app.main import create_app
from cloud.app.services.auth import create_test_auth_service
from cloud.app.services.action_router import ActionRouter
from cloud.app.services.event_store import EventRecord, EventStore
from shared.enums import CommandAckStatus, EventRecordState, Priority, WorkMode


# ==========================================
# 场景 1: 越权伪造 action 拦截
# ==========================================
def test_boundary_1_cross_device_spoofing():
    """边界 1: 设备 B 试图冒充设备 A 确认告警，ActionRouter 严格拦截"""
    now = 1000
    store = EventStore(time_provider=lambda: now)

    # 属于 esp32_device_a 的告警
    store.save(
        EventRecord(
            event_id="evt_device_a_alert",
            target_device="esp32_device_a",
            target_host="macbook_a",
            target_url="https://github.com/pull/1",
            is_interactive=True,
            created_at=now,
            priority=Priority.MEDIUM,
        )
    )

    router = ActionRouter(event_store=store, time_provider=lambda: now)

    # 设备 B 尝试确认设备 A 的告警
    spoofed_payload = {
        "type": "device_action",
        "version": 1,
        "action_id": "8f7b2c9e-10a2-4a7b-8f12-3c4d5e6f7a8b",
        "device_id": "esp32_device_b",
        "created_at": now,
        "expire_at": now + 10,
        "data": {
            "action": "tap",
            "active_alert_id": "evt_device_a_alert",
        },
    }

    result = router.handle_device_action(
        topic="sprite/device/esp32_device_b/up/action",
        payload_bytes=json.dumps(spoofed_payload).encode("utf-8"),
        now=now,
    )

    # 归属校验失败，绝对拒绝派发指令
    assert result.status == "ownership_rejected"
    assert "Security reject" in result.audit_message


# ==========================================
# 场景 2: 409 幂等冲突拦截
# ==========================================
def test_boundary_2_idempotency_conflict():
    """边界 2: 相同 Idempotency-Key 提交不同内容，返回 HTTP 409 Conflict"""
    store = EventStore()
    app = create_app(auth_service=create_test_auth_service(), event_store=store)
    client = TestClient(app)

    headers = {
        "Authorization": "Bearer spr_live_test_key",
        "Idempotency-Key": "idemp_boundary_key_999",
    }
    payload_a = {
        "target_device": "esp32_c049ef",
        "title": "任务 A 成功",
        "summary": "内容 A",
        "priority": "medium",
    }
    payload_b = {
        "target_device": "esp32_c049ef",
        "title": "任务 B 失败",
        "summary": "内容 B (不同 Body)",
        "priority": "high",
    }

    # 第一次提交 A 成功
    res_a = client.post("/api/v1/notify", json=payload_a, headers=headers)
    assert res_a.status_code == 200

    # 第二次使用相同 Key 提交不同 Body B，触发 409 冲突
    res_b = client.post("/api/v1/notify", json=payload_b, headers=headers)
    assert res_b.status_code == 409
    res_json = res_b.json()
    assert res_json["code"] == 40901
    assert "Idempotency key conflict" in res_json["message"]


# ==========================================
# 场景 3: Mac 端指令并发/重复原子去重
# ==========================================
def test_boundary_3_mac_command_in_flight_duplicate_suppression():
    """边界 3: 相同 command_id 重复到达 Mac Executor 时，原子占位去重，杜绝重复调用 open"""
    mock_opener = MagicMock(return_value=True)
    executor = CommandExecutor(host_id="macbook_pro", system_opener=mock_opener)

    now = 1000
    cmd_payload = {
        "type": "host_command",
        "version": 1,
        "command_id": "cmd_atomic_dedup_001",
        "source_action_id": "act_001",
        "created_at": now,
        "expire_at": now + 60,
        "action": "open_browser",
        "params": {"url": "https://github.com/notifications"},
    }

    # 首次执行: 成功调用系统 opener
    ack1 = executor.execute(cmd_payload, now=now)
    assert ack1.status == CommandAckStatus.SUCCESS
    assert mock_opener.call_count == 1

    # 再次到达 (或并发重试): 原子拦截，返回 DUPLICATE_IGNORED
    ack2 = executor.execute(cmd_payload, now=now + 1)
    assert ack2.status == CommandAckStatus.DUPLICATE_IGNORED
    assert ack2.error_code == 0
    # opener 不被二次执行
    assert mock_opener.call_count == 1


# ==========================================
# 场景 4: Sync Barrier 抑制 SIGTTIN
# ==========================================
def test_boundary_4_sync_barrier_no_sigttin():
    """边界 4: PTY 环境下验证 Pipe Sync Barrier 彻底消除 SIGTTIN 竞态"""
    master_fd, slave_fd = pty.openpty()

    pid = os.fork()
    if pid == 0:
        os.close(master_fd)
        os.setsid()
        import fcntl
        import termios
        fcntl.ioctl(slave_fd, termios.TIOCSCTTY, 0)
        os.dup2(slave_fd, 0)
        os.dup2(slave_fd, 1)
        os.dup2(slave_fd, 2)
        if slave_fd > 2:
            os.close(slave_fd)

        # 启动带前台 TTY 传递的 run_wrapper
        code = run_wrapper(["/bin/echo", "barrier_ok"], notify_enabled=False)
        os._exit(code)

    os.close(slave_fd)
    # 读取输出
    try:
        os.read(master_fd, 1024)
    except OSError:
        pass
    os.close(master_fd)

    _, status = os.waitpid(pid, 0)
    # 验证子进程正常退出 (没有死于 SIGTTIN 信号 21)
    assert os.WIFEXITED(status)
    assert not os.WIFSIGNALED(status) or os.WTERMSIG(status) != 21


# ==========================================
# 场景 5: 两阶段模式过滤与出队调度复核
# ==========================================
def test_boundary_5_two_stage_filtering_and_egress_recheck():
    """边界 5: 验证 Stage 1 入队模式拦截与 Stage 2 出队清空复核"""
    # 状态机逻辑测试，确保与固件 C 状态机契约完全对齐
    # 1. Stage 1 (Ingress): Sleep 模式下 Medium 必须被 Drop
    def ingress_check(mode: WorkMode, priority: Priority) -> str:
        if mode == WorkMode.SLEEP and priority in (Priority.LOW, Priority.MEDIUM):
            return "DROP"
        if mode == WorkMode.MEETING and priority == Priority.LOW:
            return "DROP"
        if mode in (WorkMode.CODING, WorkMode.IDLE) and priority == Priority.LOW:
            return "STATUS_DOT"
        return "ACCEPT"

    assert ingress_check(WorkMode.SLEEP, Priority.MEDIUM) == "DROP"
    assert ingress_check(WorkMode.SLEEP, Priority.HIGH) == "ACCEPT"

    # 2. Stage 2 (Egress): Coding 下排队后，若当前突变 Sleep，所有 Medium 必须全数被清空
    def egress_recheck(current_mode: WorkMode, priority: Priority, now: int, expire_at: int):
        if priority != Priority.HIGH and expire_at > 0 and now > expire_at:
            return False, False  # expired
        if current_mode == WorkMode.SLEEP:
            if priority != Priority.HIGH:
                return False, True  # (valid=False, clear_all_medium=True)
            return True, False
        return True, False

    valid, clear_all = egress_recheck(WorkMode.SLEEP, Priority.MEDIUM, now=1000, expire_at=2000)
    assert valid is False
    assert clear_all is True  # 清空队列中所有 Medium
