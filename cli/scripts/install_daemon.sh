#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DRY_RUN=false
for arg in "$@"; do
    if [ "$arg" == "--dry-run" ]; then
        DRY_RUN=true
    fi
done

# 确定 Python 解释器
if [ -f "${PROJECT_ROOT}/.venv/bin/python" ]; then
    PYTHON_EXEC="${PROJECT_ROOT}/.venv/bin/python"
elif [ -f "${PROJECT_ROOT}/.venv/bin/python3" ]; then
    PYTHON_EXEC="${PROJECT_ROOT}/.venv/bin/python3"
else
    PYTHON_EXEC="$(which python3)"
fi

SPRITE_DAEMON_EXEC="${PROJECT_ROOT}/cli/bin/sprite-daemon"
LOG_DIR="${HOME}/.sprite/logs"
WORKING_DIR="${PROJECT_ROOT}"
TEMPLATE_PATH="${PROJECT_ROOT}/cli/templates/com.sprite.daemon.plist.template"
TARGET_DIR="${HOME}/Library/LaunchAgents"
TARGET_PLIST="${TARGET_DIR}/com.sprite.daemon.plist"

mkdir -p -m 0700 "${LOG_DIR}"

RENDERED_PLIST=$(sed \
    -e "s|{{PYTHON_EXEC}}|${PYTHON_EXEC}|g" \
    -e "s|{{SPRITE_DAEMON_EXEC}}|${SPRITE_DAEMON_EXEC}|g" \
    -e "s|{{LOG_DIR}}|${LOG_DIR}|g" \
    -e "s|{{WORKING_DIR}}|${WORKING_DIR}|g" \
    "${TEMPLATE_PATH}")

if [ "${DRY_RUN}" = true ]; then
    echo "=== Dry-Run: Rendered Plist for ${TARGET_PLIST} ==="
    echo "${RENDERED_PLIST}"
    echo "=== End of Rendered Plist ==="
    exit 0
fi

mkdir -p "${TARGET_DIR}"

# 如果已有运行中的服务，先卸载
if [ -f "${TARGET_PLIST}" ]; then
    echo "Unloading existing daemon..."
    launchctl unload "${TARGET_PLIST}" 2>/dev/null || true
fi

# 写入 plist
echo "${RENDERED_PLIST}" > "${TARGET_PLIST}"
chmod 0644 "${TARGET_PLIST}"

# 重新加载服务
echo "Loading daemon via launchctl..."
launchctl load "${TARGET_PLIST}"

echo "Desk Sprite Daemon installed successfully to ${TARGET_PLIST}."
