#!/usr/bin/env python3
"""Mosquitto 严格 Topic ACL 规则生成器

依据规范: SPEC-002 第 1 节
功能:
1. 规范化实体标识符 (严格剥离多余的前缀，杜绝 host_host_ 或 dev_dev_ 二次拼接)
2. 依据实体角色生成最小权限 ACL 规则条目
3. 支持独立单元自测 (--test)
"""

import argparse
import sys
from typing import Dict, List, Set


def normalize_id(entity_id: str, prefix_to_strip: str) -> str:
    """去除可能误传入的前缀，确保纯标识符命名"""
    entity_id = entity_id.strip()
    while entity_id.startswith(prefix_to_strip):
        entity_id = entity_id[len(prefix_to_strip):]
    return entity_id


def generate_acl_rules(hosts: List[str], devices: List[str], admin_user: str = "cloud_admin") -> str:
    """根据 host_id 和 device_id 列表生成标准的 Mosquitto ACL 内容"""
    lines: List[str] = [
        "# =====================================================================",
        "# Desk Sprite Mosquitto ACL Configuration",
        "# 依据规范: SPEC-002 第 1 节 通信架构、身份标识与 Topic 命名空间",
        "# =====================================================================",
        "",
        "# 1. 云端引擎 (Superuser - 全 Topic 读写)",
        f"user {admin_user}",
        "topic readwrite #",
        "",
    ]

    # 去重并标准化 host 列表
    normalized_hosts: Set[str] = {normalize_id(h, "host_") for h in hosts if h.strip()}
    if normalized_hosts:
        lines.append("# 2. Mac 宿主权限隔离 (仅读下行，仅写上行)")
        for host_id in sorted(normalized_hosts):
            username = f"host_{host_id}"
            lines.append(f"user {username}")
            lines.append(f"topic read sprite/host/{host_id}/down/#")
            lines.append(f"topic write sprite/host/{host_id}/up/#")
            lines.append("")

    # 去重并标准化 device 列表
    normalized_devices: Set[str] = {normalize_id(d, "dev_") for d in devices if d.strip()}
    if normalized_devices:
        lines.append("# 3. ESP32 终端权限隔离 (仅读下行，仅写上行)")
        for dev_id in sorted(normalized_devices):
            username = f"dev_{dev_id}"
            lines.append(f"user {username}")
            lines.append(f"topic read sprite/device/{dev_id}/down/#")
            lines.append(f"topic write sprite/device/{dev_id}/up/#")
            lines.append("")

    return "\n".join(lines).strip() + "\n"


def run_self_tests() -> bool:
    """运行 ACL 生成器自测逻辑"""
    print("[TEST] Running generate_acl self-tests...")

    # 1. 验证前缀去除与杜绝重复拼接
    assert normalize_id("macbook_pro", "host_") == "macbook_pro"
    assert normalize_id("host_macbook_pro", "host_") == "macbook_pro"
    assert normalize_id("host_host_macbook_pro", "host_") == "macbook_pro"
    assert normalize_id("dev_esp32_c049ef", "dev_") == "esp32_c049ef"
    assert normalize_id("esp32_c049ef", "dev_") == "esp32_c049ef"

    # 2. 验证规则生成准确性
    hosts = ["macbook_pro", "host_secondary_mac"]
    devices = ["esp32_c049ef", "dev_esp32_test"]
    acl_output = generate_acl_rules(hosts, devices)

    assert "user cloud_admin\ntopic readwrite #" in acl_output
    assert "user host_macbook_pro" in acl_output
    assert "topic read sprite/host/macbook_pro/down/#" in acl_output
    assert "topic write sprite/host/macbook_pro/up/#" in acl_output

    assert "user host_secondary_mac" in acl_output
    assert "user host_host_secondary_mac" not in acl_output

    assert "user dev_esp32_c049ef" in acl_output
    assert "topic read sprite/device/esp32_c049ef/down/#" in acl_output
    assert "topic write sprite/device/esp32_c049ef/up/#" in acl_output
    assert "user dev_dev_esp32_test" not in acl_output
    assert "user dev_esp32_test" in acl_output

    print("[TEST] All self-tests passed successfully!")
    return True


def main():
    parser = argparse.ArgumentParser(description="Desk Sprite Mosquitto ACL 生成工具")
    parser.add_argument("--test", action="store_true", help="运行内部单元测试")
    parser.add_argument("--output", "-o", default="cloud/deploy/mosquitto/acl.conf", help="输出文件路径")
    parser.add_argument("--hosts", nargs="*", default=["macbook_pro"], help="Mac 宿主 ID 列表")
    parser.add_argument("--devices", nargs="*", default=["esp32_c049ef"], help="ESP32 终端 ID 列表")

    args = parser.parse_args()

    if args.test:
        if run_self_tests():
            sys.exit(0)
        else:
            sys.exit(1)

    acl_content = generate_acl_rules(args.hosts, args.devices)
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(acl_content)
    print(f"[OK] ACL configuration generated at: {args.output}")


if __name__ == "__main__":
    main()
