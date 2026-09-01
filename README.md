# Codex ESP32 Display

A physical **Codex attention inbox** for the rectangular
[Waveshare ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06)
(410×502 AMOLED, touch, BOOT button, and PWR button).

It intentionally does not show every historical thread. It shows threads that
need a human glance:

- Codex is waiting for an approval;
- Codex is waiting for user input;
- the thread is unread, including a thread manually marked unread;
- the thread is pinned.

Waiting threads come first, then newly completed unread work, other unread
threads, and pinned threads. A thread appears once even when several reasons
apply.

## Controls

- **BOOT short press:** highlight the next thread, wrapping at the end.
- **PWR short press:** open the highlighted thread's latest text.
- **PWR on the text screen:** return to the inbox.
- **BOOT on the text screen:** jump to the next thread and load its latest text.
- **Touch:** scroll either screen; tap a thread card to open it.

The firmware only observes the AXP2101 short-press status. It does not change the
board's long-hold hardware shutdown behavior.

## What is implemented

### Mac bridge

- launches `codex app-server` over JSONL stdio;
- reads thread names, projects, timestamps, live status, and pinned-section data;
- reads Codex Desktop's local unread-ID set **read-only**;
- observes `turn/completed` and status notifications;
- exposes a bearer-token-protected attention-list endpoint;
- exposes an on-demand latest-text endpoint for threads currently in the inbox;
- includes a browser dashboard with the same list/detail interaction;
- reconnects when App Server exits and preserves the last good list.

The bridge has no runtime npm dependencies.

### ESP-IDF firmware

- targets the Waveshare ESP32-S3-Touch-AMOLED-2.06 only;
- uses Waveshare's managed BSP and LVGL 9.5;
- renders a touch-scrollable 410×502 inbox;
- preserves the selected thread across list refreshes;
- reads BOOT on GPIO0 and the PWR short-press latch from the AXP2101 over the
  BSP's shared I²C bus;
- fetches long thread text in a separate FreeRTOS task, so the UI remains usable;
- discards stale detail responses after the user changes threads;
- keeps the last good list when Wi-Fi or the bridge temporarily fails.

## Architecture

```text
Codex Desktop state                 codex app-server
~/.codex/.codex-global-state.json  JSONL over stdio
          │ unread IDs                    │ thread metadata/status/turns
          └──────────────┬─────────────────┘
                         ▼
                 Mac bridge :5180
        /api/v1/attention    /api/v1/threads/:id/latest
                         │ Bearer token / LAN
                         ▼
         Waveshare ESP32-S3-Touch-AMOLED-2.06
        touch + BOOT/PWR list selection and detail view
```

The bridge never edits Codex state. Opening text on the ESP32 does **not** mark a
thread read in Codex Desktop.

## 1. Start the Mac bridge

Prerequisites:

- macOS with Codex Desktop or Codex CLI installed;
- Codex CLI on `PATH`, or bundled inside `ChatGPT.app`/legacy `Codex.app`;
- Node.js 18.18 or newer.

```bash
git clone https://github.com/seichris/codex-esp32-display.git
cd codex-esp32-display/bridge
npm run setup
npm start
```

`npm run setup` creates `bridge/config.json` with a random 256-bit bearer token.
It refuses to overwrite an existing config.

Open the browser dashboard:

```text
http://127.0.0.1:5180/
```

The ESP32 list endpoint is:

```text
http://<mac-lan-ip>:5180/api/v1/attention
```

A typical macOS Wi-Fi address can be printed with:

```bash
ipconfig getifaddr en0
```

Allow incoming Node connections if the macOS firewall asks.

### Bridge configuration

`bridge/config.json`:

```json
{
  "host": "0.0.0.0",
  "port": 5180,
  "token": "generated-by-npm-run-setup",
  "pollIntervalMs": 2000,
  "maxThreads": 300,
  "maxItems": 30,
  "codexBin": "codex",
  "codexHome": "~/.codex"
}
```

Environment overrides are also supported:
`CODEX_ATTENTION_CONFIG`, `CODEX_ATTENTION_HOST`,
`CODEX_ATTENTION_PORT`, `CODEX_ATTENTION_TOKEN`,
`CODEX_ATTENTION_POLL_MS`, `CODEX_BIN`, and `CODEX_HOME`.

### Run at login

```bash
./scripts/install-macos-launch-agent.sh
```

Logs are written under `~/Library/Logs/CodexESP32Display/`. Remove the service
with:

```bash
./scripts/uninstall-macos-launch-agent.sh
```

## 2. Flash the Waveshare board

Install and export ESP-IDF 5.4 or newer, then:

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig
```

Under **Codex ESP32 Display**, set:

1. Wi-Fi SSID and password;
2. the full attention endpoint printed during bridge setup;
3. the same bearer token.

Then:

```bash
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

The first build downloads the official
`waveshare/esp32_s3_touch_amoled_2_06` BSP and LVGL through the ESP-IDF
Component Manager.

## 3. Optional macOS menu-bar companion

The native SwiftUI companion owns the bridge process and shows a small outline
of the 2.06 device in the macOS menu bar:

```bash
./macos/build_app.sh
open "macos/build/Codex ESP32 Display.app"
```

It replaces the bridge LaunchAgent while running and provides bridge status,
start/stop, the dashboard, endpoint copying, and log access from its menu.

## Attention rules

```text
waiting_for_approval
OR waiting_for_user_input
OR unread
OR pinned
```

Priority:

1. waiting for approval;
2. waiting for user input;
3. newly completed and unread while the bridge was running;
4. any other unread thread;
5. pinned thread;
6. newest update first within a group.

After a bridge restart, an unread result still appears as **UNREAD**, but only a
completion observed live by that bridge process gets the **NEW** badge. The
persisted unread set does not record why a thread became unread.

## API

List:

```bash
curl -H "Authorization: Bearer <token>" \
  http://127.0.0.1:5180/api/v1/attention
```

Latest text for a thread currently in that list:

```bash
curl -H "Authorization: Bearer <token>" \
  http://127.0.0.1:5180/api/v1/threads/<thread-id>/latest
```

The latest-text response prefers the newest Codex agent message, then a plan,
then a user message, then the thread preview. Text is capped at 5,600 UTF-8 bytes
for predictable ESP32 memory use.

See [docs/protocol.md](docs/protocol.md) and
[docs/architecture.md](docs/architecture.md).

## Security and compatibility

- HTTP plus a bearer token prevents accidental access; it does not encrypt LAN
  traffic. Use a trusted/private network.
- Unread state comes from Codex Desktop's internal
  `.codex-global-state.json`. Parsing is read-only and failure-tolerant, but the
  format can change.
- Current pinned sections, detailed status, and paginated turns depend on Codex
  App Server APIs. The bridge falls back to `thread/read` for latest text when
  turn pagination is unavailable.
- The PWR input path is implemented against the AXP2101 short-press status latch;
  physical-button behavior should still be verified on the exact board revision
  before treating it as production hardware.

## Development

```bash
cd bridge
npm ci
npm test
npm run check
```

GitHub Actions runs the bridge suite and an ESP-IDF 5.4.4 firmware build.

## License

MIT. See [NOTICE](NOTICE) for trademark and affiliation information.
