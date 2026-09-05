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

## Device dictation

Open **Voice Settings… → Enable Dictation Permissions**. Both Microphone and
Speech Recognition must be allowed; Accessibility is no longer used for
recording or draft handoff. On-device English speech recognition must be
available. The microphone is selected by its full Waveshare USB unique ID;
the companion never falls back to the Mac's built-in input or changes the
system default microphone.

1. Select a task on the device and hold either button for one second.
2. Wait for `LISTENING`. That acknowledgement requires a real USB sample buffer
   from an active capture session, not merely a posted keyboard shortcut.
3. Speak, then hold again. This closes the device PCM gate and calls
   `endAudio()` on the local speech request so transcription can finish.
4. Completed dictation automatically opens in the recorded task's composer,
   using its task ID and a percent-encoded `prompt` query in a Codex deep link.
   This replaces any existing composer text; it never sends a message.
   The companion keeps its copy until **Discard**, with **Copy Text** as a fallback.
   **Open as Task Draft** can retry the handoff or reopen an edited copy. Failed
   recognition stays in Device Dictation for review rather than opening automatically.

The exact target is pinned for a recording. Switching targets while recording,
overwriting a pending draft, and late callbacks from previous recordings are
rejected. Recording ends after 55 seconds; finalization has a 10-second timeout
and retains partial text on failure. Empty speech results, including an empty final
result after stopping, preserve the latest non-empty transcript for review. Late
callbacks cannot overwrite the editable draft, and starting another recording
requires discarding any existing text first. The existing firmware uses `VOICE MUTED`
while the companion finishes transcription and `VOICE READY` when a draft exists.
The updated firmware closes its gate when a fresh companion snapshot reports
that recording ended or became unavailable, and enforces a 60-second local
limit even if networking fails. Older firmware still needs the second physical
press to close the gate after automatic host-side completion.

The running app with bundle ID `com.openai.codex` is preferred when opening task
links, because a newer ChatGPT.app and an older Codex.app can coexist. If multiple
copies are running and none is active, handoff fails rather than guessing.
URL acceptance is not treated as proof that the composer displayed the text.
Manual task selection now follows Codex's local task-presentation events. The
companion verifies the local broker, tracks exact task IDs per source client,
reconnects on failures, and clears ambiguous or unavailable selection. Multiple
presented task views are treated as ambiguous; remote task hosts are not yet
supported. This private interface is version checked and requires no Accessibility
permission. Voice Settings → **Show Detection Log** reveals the bounded
operational log. See [`docs/focused-task-sync.md`](../docs/focused-task-sync.md).

Diagnostics are written to
`~/Library/Logs/CodexESP32Display/dictation.log`: timestamps, stages, sample
counts and input peaks only. No audio, transcript text, task IDs or credentials
are logged. Inspect that file for capture/finalization evidence; old permission
errors in `bridge.log` are not evidence of a new dictation failure.

Local validation: `swift test --package-path macos` covers session ownership,
stale callbacks, partial-text preservation, no-speech failures, stop/start races,
and exact target/text URL encoding. The release app build and live USB speech
acceptance remain separate checks.

The private bridge/controller channel is a per-launch, mode-0700 temporary
directory with a random token. Desktop-control commands are not exposed as an
unauthenticated local socket.

The build embeds the bridge source and current `bridge/config.json` in the app
bundle so a Finder launch does not depend on access to the protected workspace.
Rebuild after changing the bridge config.

The previous `com.seichris.codex-esp32-display` LaunchAgent should remain
unloaded while this app owns port `5180`.

## Stable local signing

For repeated local builds, choose an existing Apple Development signing identity
using `CODEX_DISPLAY_SIGN_IDENTITY` or place its fingerprint in the ignored
`macos/.signing-identity` file. The build fails if the chosen identity cannot sign;
it does not silently fall back to ad hoc signing. Without a selection, the build
uses an ad hoc signature and warns that privacy permissions may need reapproval
when the binary changes. No certificate or private key is stored in this repo.

Changing from ad hoc to certificate signing requires one fresh permission grant.
Keep the app's installation path and signing identity stable for later updates.
The build verifies its completed signature. Do not weaken the designated
requirement to an identifier-only rule to work around privacy checks.
