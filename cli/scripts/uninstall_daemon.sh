#!/bin/bash
set -euo pipefail

TARGET_PLIST="${HOME}/Library/LaunchAgents/com.sprite.daemon.plist"

if [ -f "${TARGET_PLIST}" ]; then
    echo "Unloading daemon from launchctl..."
    launchctl unload "${TARGET_PLIST}" 2>/dev/null || true
    rm -f "${TARGET_PLIST}"
    echo "Removed ${TARGET_PLIST}"
else
    echo "Daemon plist not found at ${TARGET_PLIST}"
fi

echo "Desk Sprite Daemon uninstalled successfully."
