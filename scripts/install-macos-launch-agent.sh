#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BRIDGE="$ROOT/bridge"
CONFIG="$BRIDGE/config.json"
NODE="$(command -v node || true)"
CODEX="$(command -v codex || true)"
LABEL="com.seichris.codex-esp32-display"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/CodexESP32Display"

if [[ -z "$NODE" ]]; then
  echo "node was not found on PATH" >&2
  exit 1
fi
if [[ -z "$CODEX" ]]; then
  for candidate in \
    "/Applications/ChatGPT.app/Contents/Resources/codex" \
    "/Applications/Codex.app/Contents/Resources/codex" \
    "$HOME/Applications/ChatGPT.app/Contents/Resources/codex" \
    "$HOME/Applications/Codex.app/Contents/Resources/codex" \
    "$HOME/.local/bin/codex"; do
    if [[ -x "$candidate" ]]; then CODEX="$candidate"; break; fi
  done
fi
if [[ -z "$CODEX" ]]; then
  echo "codex was not found on PATH or inside ChatGPT.app/Codex.app" >&2
  exit 1
fi
if [[ ! -f "$CONFIG" ]]; then
  echo "Missing $CONFIG; run 'cd bridge && npm run setup' first." >&2
  exit 1
fi

mkdir -p "$(dirname "$PLIST")" "$LOG_DIR"
python3 - "$PLIST" "$LABEL" "$NODE" "$CODEX" "$BRIDGE" "$LOG_DIR" <<'PY'
import os, plistlib, sys
path, label, node, codex, bridge, logs = sys.argv[1:]
body = {
    "Label": label,
    "ProgramArguments": [node, f"{bridge}/src/index.mjs"],
    "WorkingDirectory": bridge,
    "RunAtLoad": True,
    "KeepAlive": True,
    "ProcessType": "Background",
    "StandardOutPath": f"{logs}/bridge.log",
    "StandardErrorPath": f"{logs}/bridge-error.log",
    "EnvironmentVariables": {
        "PATH": f"{os.path.expanduser('~/.local/bin')}:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin",
        "CODEX_BIN": codex,
    },
}
with open(path, "wb") as f:
    plistlib.dump(body, f)
PY

launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.seichris.codex-attention-display.plist" 2>/dev/null || true
launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
launchctl kickstart -k "gui/$(id -u)/$LABEL"
echo "Installed and started $LABEL"
echo "Logs: $LOG_DIR"
