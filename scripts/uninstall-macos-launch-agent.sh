#!/usr/bin/env bash
set -euo pipefail
LABEL="com.seichris.codex-esp32-display"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LEGACY_PLIST="$HOME/Library/LaunchAgents/com.seichris.codex-attention-display.plist"
launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
launchctl bootout "gui/$(id -u)" "$LEGACY_PLIST" 2>/dev/null || true
rm -f "$PLIST" "$LEGACY_PLIST"
echo "Removed $LABEL"
