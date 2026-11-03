"""Mac 守护进程生命周期与 PID 管理测试 (TASK-012)"""

import os
import signal
import subprocess
import sys
import time
from pathlib import Path
import pytest


@pytest.fixture
def clean_daemon_env(monkeypatch, tmp_path):
    """隔离 ~/.sprite 目录到测试临时目录"""
    fake_sprite = tmp_path / ".sprite"
    fake_sprite.mkdir(parents=True, mode=0o700)
    monkeypatch.setenv("HOME", str(tmp_path))
    return fake_sprite


def test_daemon_status_when_stopped(clean_daemon_env):
    """守护进程未运行时 status 应返回 1"""
    cmd = [sys.executable, "cli/bin/sprite-daemon", "status"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 1
    assert "Daemon is stopped" in res.stdout


def test_daemon_status_with_stale_pid(clean_daemon_env):
    """存在已失效的 PID 文件时，status 识别为 stopped 并清理或报告 stale"""
    pid_file = clean_daemon_env / "daemon.pid"
    # 写入一个肯定不存在的 PID
    pid_file.write_text("999999")

    cmd = [sys.executable, "cli/bin/sprite-daemon", "status"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 1
    assert "stale PID" in res.stdout or "stopped" in res.stdout


def test_daemon_stop_when_not_running(clean_daemon_env):
    """未运行时执行 stop 命令安全退出"""
    cmd = [sys.executable, "cli/bin/sprite-daemon", "stop"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 0
    assert "Daemon is not running" in res.stdout or "removed" in res.stdout


def test_daemon_run_pid_lock_and_stop(clean_daemon_env):
    """测试 PID 文件写入、重复运行拦截与 stop 优雅退出"""
    pid_file = clean_daemon_env / "daemon.pid"

    # 伪造一个存活的 mock 守护进程 (使用 sleep 30)
    proc = subprocess.Popen(["sleep", "30"])
    pid_file.write_text(str(proc.pid))

    # 1. 此时 status 检查应为 running
    status_res = subprocess.run(
        [sys.executable, "cli/bin/sprite-daemon", "status"],
        capture_output=True,
        text=True,
    )
    assert status_res.returncode == 0
    assert f"Daemon is running (PID: {proc.pid})" in status_res.stdout

    # 2. 尝试再次 run 应被 PID 冲突拦截
    run_dup = subprocess.run(
        [sys.executable, "cli/bin/sprite-daemon", "run"],
        capture_output=True,
        text=True,
    )
    assert run_dup.returncode == 1

    # 3. 执行 stop 命令，应向其发送 SIGTERM 并清除 PID 文件
    stop_res = subprocess.run(
        [sys.executable, "cli/bin/sprite-daemon", "stop"],
        capture_output=True,
        text=True,
    )
    assert stop_res.returncode == 0
    assert "Daemon stopped." in stop_res.stdout
    assert not pid_file.exists()

    # 验证原进程已死亡
    proc.poll()
    assert proc.returncode is not None or not os.path.exists(f"/proc/{proc.pid}")


from cli.sprite.config import SpriteConfig, save_config


def test_daemon_real_run_startup_and_sigterm(clean_daemon_env):
    """R04 验证: 真实执行 sprite-daemon run，不再抛出 AttributeError，并响应 SIGTERM 优雅退出"""
    config_file = clean_daemon_env / "config.json"
    cfg = SpriteConfig(
        host_id="test_mac",
        cloud_endpoint="http://127.0.0.1:8000",
        mqtt_broker="127.0.0.1",
        mqtt_port=1883,
        mqtt_username="host_test_mac",
        mqtt_password="secret_password",
    )
    save_config(cfg, config_file)

    env = os.environ.copy()
    env["HOME"] = str(clean_daemon_env.parent)
    env["PYTHONPATH"] = "."

    proc = subprocess.Popen(
        [sys.executable, "cli/bin/sprite-daemon", "run"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    try:
        pid_file = clean_daemon_env / "daemon.pid"
        for _ in range(30):
            if pid_file.exists():
                break
            time.sleep(0.1)
        assert pid_file.exists()

        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=3.0)
        assert proc.returncode == 0
        assert not pid_file.exists()
    finally:
        if proc.poll() is None:
            proc.kill()

