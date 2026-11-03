"""sprite run 命令包装器与管道同步屏障 (SPEC-004 第 2.2 节, H02 闭环)

核心机制:
1. TTY 交互环境: 管道单向同步屏障 (Pipe-based Sync Barrier) 彻底消灭 SIGTTIN 挂起
2. 异常兜底: tcsetpgrp 移交失败立即向子进程发送 SIGKILL，杜绝悬挂僵尸进程
3. 非 TTY 环境: 直接透传 FD 0/1/2，零损流式透传
4. 退出码精确保持: 子进程退出码与信号编号 (128 + signum) 语义不变
5. 通知硬超时: 3.0s 超时保护，失败仅向 stderr 告警，绝不篡改原退出码
"""

import os
import signal
import sys
import time
from typing import List, Optional

from cli.sprite.commands.notify import run_notify


def run_wrapper(
    command_args: List[str],
    notify_enabled: bool = True,
    delay_handoff_for_test_ms: int = 0,
    force_handoff_fail_for_test: bool = False,
) -> int:
    if not command_args:
        return 0

    cmd_display = " ".join(command_args)
    cmd_name = os.path.basename(command_args[0])
    start_time = time.time()

    # 依据 SPEC-004 第 2.2 节: 严格基于标准输入文件描述符 0 (STDIN_FILENO) 检测是否处于 TTY
    stdin_fd = 0
    try:
        is_tty = os.isatty(stdin_fd)
    except Exception:
        is_tty = False

    if is_tty:
        # 1. TTY 控制终端交互环境
        pipe_r, pipe_w = os.pipe()

        try:
            pid = os.fork()
        except OSError as e:
            sys.stderr.write(f"Fork failed: {e}\n")
            os.close(pipe_r)
            os.close(pipe_w)
            return 1

        if pid == 0:
            # === 子进程逻辑 ===
            try:
                os.close(pipe_w)
                os.setpgid(0, 0)

                # 阻塞屏障: 等待父进程完成 tcsetpgrp 移交并写入放行 token
                token = os.read(pipe_r, 1)
                os.close(pipe_r)

                if token != b"G":
                    os._exit(1)

                # 屏障解除，独占前台控制权，安全执行目标命令，绝不触发 SIGTTIN
                os.execvp(command_args[0], command_args)
            except FileNotFoundError:
                os._exit(127)
            except PermissionError:
                os._exit(126)
            except Exception as e:
                sys.stderr.write(f"Exec failed: {e}\n")
                os._exit(127)

        # === 父进程逻辑 ===
        os.close(pipe_r)
        old_sigttou = signal.signal(signal.SIGTTOU, signal.SIG_IGN)

        try:
            # 测试用模拟人为交接延迟
            if delay_handoff_for_test_ms > 0:
                time.sleep(delay_handoff_for_test_ms / 1000.0)

            os.setpgid(pid, pid)

            if force_handoff_fail_for_test:
                raise OSError("Simulated tcsetpgrp failure for test")

            os.tcsetpgrp(stdin_fd, pid)
            handoff_success = True
        except Exception as e:
            sys.stderr.write(f"Failed to hand off foreground control: {e}\n")
            # 移交失败兜底: SIGKILL 子进程，清理退出，绝不留悬挂进程
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
            os.close(pipe_w)
            signal.signal(signal.SIGTTOU, old_sigttou)
            return 1

        # 放行子进程
        os.write(pipe_w, b"G")
        os.close(pipe_w)

        # 等待子进程结束
        try:
            _, status = os.waitpid(pid, 0)
        except KeyboardInterrupt:
            # 父进程若收到 Ctrl-C，等待子进程退出
            try:
                _, status = os.waitpid(pid, 0)
            except OSError:
                status = 130 << 8

        # 回收前台控制权
        try:
            os.tcsetpgrp(stdin_fd, os.getpgrp())
        except OSError:
            pass
        signal.signal(signal.SIGTTOU, old_sigttou)

    else:
        # 2. 非 TTY 环境 (管道 / 重定向 / CI)
        # 直接透传 FD 0/1/2，父进程转发 SIGINT/SIGTERM
        try:
            pid = os.fork()
        except OSError as e:
            sys.stderr.write(f"Fork failed: {e}\n")
            return 1

        if pid == 0:
            # R11: 创建独立子进程组，使信号能转发给包含 shell 后代在内的整个进程树
            try:
                os.setpgid(0, 0)
            except OSError:
                pass

            try:
                os.execvp(command_args[0], command_args)
            except FileNotFoundError:
                os._exit(127)
            except PermissionError:
                os._exit(126)
            except Exception as e:
                sys.stderr.write(f"Exec failed: {e}\n")
                os._exit(127)

        # 父进程尝试确保子进程组设置完毕，避免竞态
        try:
            os.setpgid(pid, pid)
        except OSError:
            pass

        def forward_signal(signum, frame):
            try:
                # 向子进程组转发信号，确保连同后代进程（如 shell 产生的 sleep）一并退出 (R11)
                os.killpg(pid, signum)
            except OSError:
                try:
                    os.kill(pid, signum)
                except OSError:
                    pass

        old_sigint = signal.signal(signal.SIGINT, forward_signal)
        old_sigterm = signal.signal(signal.SIGTERM, forward_signal)

        try:
            _, status = os.waitpid(pid, 0)
        except KeyboardInterrupt:
            try:
                os.killpg(pid, signal.SIGINT)
            except OSError:
                pass
            try:
                _, status = os.waitpid(pid, 0)
            except OSError:
                status = 130 << 8
        finally:
            signal.signal(signal.SIGINT, old_sigint)
            signal.signal(signal.SIGTERM, old_sigterm)

    # 3. 计算最终退出码
    if os.WIFEXITED(status):
        exit_code = os.WEXITSTATUS(status)
    elif os.WIFSIGNALED(status):
        signum = os.WTERMSIG(status)
        exit_code = 128 + signum
    else:
        exit_code = 1

    elapsed = time.time() - start_time
    elapsed_str = f"{elapsed:.1f}s" if elapsed < 60 else f"{int(elapsed//60)}m{int(elapsed%60)}s"

    # 4. 视听联动通知 (3.0s 硬超时保护，绝不篡改原退出码)
    if notify_enabled:
        if exit_code == 0:
            title = f"{cmd_name} 执行完成"[:24]
            summary = f"耗时 {elapsed_str} · 命令成功"[:48]
            level = "medium"
            sound = "ding"
            mood = "cheer"
        else:
            title = f"{cmd_name} 异常退出 ({exit_code})"[:24]
            summary = f"耗时 {elapsed_str} · 请检查输出"[:48]
            level = "high"
            sound = "alert_beep"
            mood = "sigh"

        try:
            run_notify(
                title=title,
                summary=summary,
                level=level,
                sound=sound,
                mood=mood,
                timeout=3.0,
            )
        except Exception as e:
            sys.stderr.write(f"Warning: Failed to send sprite run notification: {e}\n")

    return exit_code
