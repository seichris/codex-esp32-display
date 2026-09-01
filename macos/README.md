# Codex ESP32 Display menu-bar app

This native SwiftUI companion owns the local Node bridge and exposes it as a
macOS menu-bar app. Its status-item icon is a monochrome filled silhouette of
the 2.06 device.

Build the app from the repository root:

```bash
./macos/build_app.sh
open "macos/build/Codex ESP32 Display.app"
```

The app starts `bridge/src/index.mjs` and writes its output to
`~/Library/Logs/CodexESP32Display/bridge.log`. The menu provides bridge status,
start/stop, the local dashboard, endpoint copying, log reveal, and quit.

The build embeds the bridge source and current `bridge/config.json` in the app
bundle so a Finder launch does not depend on access to the protected workspace.
Rebuild after changing the bridge config.

The previous `com.seichris.codex-esp32-display` LaunchAgent should remain
unloaded while this app owns port `5180`.
