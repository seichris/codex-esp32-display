#!/usr/bin/env bash
set -euo pipefail
LABEL="com.seichris.codex-attention-display"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
rm -f "$PLIST"
echo "Removed $LABEL"
