#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MACOS_ROOT="$ROOT/macos"
APP="$MACOS_ROOT/build/Codex ESP32 Display.app"

swift build --package-path "$MACOS_ROOT" --configuration release --product CodexESP32Display
BIN_DIR="$(swift build --package-path "$MACOS_ROOT" --configuration release --show-bin-path)"

mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/CodexESP32Display" "$APP/Contents/MacOS/CodexESP32Display"
cp "$MACOS_ROOT/Info.plist" "$APP/Contents/Info.plist"

if [[ ! -f "$ROOT/bridge/config.json" ]]; then
  printf 'Missing bridge/config.json; run npm run setup in bridge first.\n' >&2
  exit 1
fi
rm -rf "$APP/Contents/Resources/bridge"
mkdir -p "$APP/Contents/Resources/bridge"
ditto "$ROOT/bridge/src" "$APP/Contents/Resources/bridge/src"
cp "$ROOT/bridge/config.json" "$APP/Contents/Resources/bridge/config.json"
chmod +x "$APP/Contents/MacOS/CodexESP32Display"

codesign --force --deep --sign - "$APP" >/dev/null
printf 'Built %s\n' "$APP"
