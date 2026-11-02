"""sprite run 管道同步屏障单元测试 (TASK-009, H02 闭环)

测试矩阵:
1. 非 TTY 环境: 命令正常退出、非零退出码传递、命令不存在 (127)
2. PTY 真实控制终端同步屏障验证:
   - 模拟父进程交接延迟 200ms
   - 子进程在同步管道安全阻塞，移交完成后正常读取终端输入并退出，绝不触发 SIGTTIN
3. 交接失败兜底测试:
   - 模拟 tcsetpgrp 人为失败，父进程向子进程发送 SIGKILL，清理退出，无后台僵尸进程
"""

import os
import pty
import sys
import time

import pytest

from cli.sprite.commands.run import run_wrapper


def test_non_tty_basic_exit_codes():
    # 1. 成功退出 0
    code = run_wrapper(["true"], notify_enabled=False)
    assert code == 0

    # 2. 失败退出 42
    code_42 = run_wrapper(["bash", "-c", "exit 42"], notify_enabled=False)
    assert code_42 == 42

    # 3. 命令不存在 127
    code_127 = run_wrapper(["non_existent_command_xyz_123"], notify_enabled=False)
    assert code_127 == 127


def test_pty_sync_barrier_no_sigttin():
    """在真实 PTY 中验证同步屏障：故意延迟 200ms 放行，子进程交互不触发 SIGTTIN"""
    pid, fd = pty.fork()
    if pid == 0:
        # 子进程中执行包装器，故意增加 200ms 移交延迟
        cmd = ["bash", "-c", "read line && echo GOT:$line"]
        exit_code = run_wrapper(cmd, notify_enabled=False, delay_handoff_for_test_ms=200)
        os._exit(exit_code)

    # 主测试进程逻辑: 写入终端输入
    time.sleep(0.1)
    os.write(fd, b"hello_desk_sprite\n")
    time.sleep(0.3)

    # 读取 PTY 缓冲区以解除终端关闭阻塞
    output = os.read(fd, 1024)
    _, status = os.waitpid(pid, 0)
    os.close(fd)

    # 验证子进程以 0 退出，绝未被 SIGTTIN 挂起
    assert os.WIFEXITED(status)
    assert os.WEXITSTATUS(status) == 0
    assert b"GOT:hello_desk_sprite" in output


def test_handoff_failure_kills_child_cleanly():
    """验证移交前台失败时立即 SIGKILL 子进程，不留挂死进程"""
    pid, fd = pty.fork()
    if pid == 0:
        cmd = ["sleep", "10"]
        exit_code = run_wrapper(cmd, notify_enabled=False, force_handoff_fail_for_test=True)
        os._exit(exit_code)

    time.sleep(0.1)
    _, status = os.waitpid(pid, 0)
    os.close(fd)

    # 包装器捕获移交失败后以 1 退出
    assert os.WIFEXITED(status)
    assert os.WEXITSTATUS(status) == 1
