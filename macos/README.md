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
start/stop, the local dashboard, endpoint copying, log reveal, Desktop Voice
status, Voice Settings, and quit.

Desktop Voice control is deliberately split between supported local mechanisms:

- the companion asks Codex Desktop to focus a thread using its configurable
  `codex://threads/{threadId}` URL template;
- after that focus request succeeds, it invokes the configured Codex Voice
  keyboard shortcut (default: Control-Option-Space);
- the ESP32 USB microphone remains enumerated, while firmware switches its PCM
  stream between live audio and silence for listen/mute.

Open **Voice Settings…** from the menu to change the shortcut or thread URL
template. Accessibility permission is required for shortcut injection. The
companion reports URL-based focus as inferred rather than confirmed because
Codex Desktop does not currently expose a public selected-thread/Voice API.

The private bridge/controller channel is a per-launch, mode-0700 temporary
directory with a random token. Desktop-control commands are not exposed as an
unauthenticated local socket.

The build embeds the bridge source and current `bridge/config.json` in the app
bundle so a Finder launch does not depend on access to the protected workspace.
Rebuild after changing the bridge config.

The previous `com.seichris.codex-esp32-display` LaunchAgent should remain
unloaded while this app owns port `5180`.
