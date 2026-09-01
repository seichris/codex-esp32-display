# Codex Attention Display

A small, always-on **Codex attention inbox** for the
[Waveshare ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06).
It does not try to show every live agent. It shows the threads that deserve a
human glance:

- Codex is waiting for an approval;
- Codex is waiting for user input;
- the thread is unread, including a thread manually marked unread;
- the thread is pinned.

Waiting threads come first, followed by newly completed unread work, other
unread threads, and then pinned threads. A thread with several reasons appears
once with several badges.

## What is implemented

- **Mac bridge with no runtime npm dependencies**
  - launches `codex app-server` over its documented JSONL stdio transport;
  - reads thread names, projects, timestamps, status flags, and current pinned
    section metadata;
  - reads Codex Desktop's local unread-ID set **read-only**;
  - listens for `turn/completed` and thread-status notifications;
  - exposes a token-protected LAN JSON endpoint and a browser dashboard;
  - reconnects when App Server exits and keeps the latest in-memory snapshot.
- **ESP-IDF firmware for the rectangular 410×502 AMOLED board**
  - uses Waveshare's official managed board-support component;
  - initializes the CO5300 AMOLED and FT3168 touch through the BSP;
  - connects over Wi-Fi and polls the bridge;
  - renders touch-scrollable attention cards with approval, input, new, unread,
    pinned, running, and error badges.
- **Tests and CI**
  - Node tests cover state parsing, ranking, deduplication, payload limits, and
    API authentication;
  - GitHub Actions builds both the bridge tests and the ESP-IDF firmware.

## Architecture

```text
Codex Desktop state                 codex app-server
~/.codex/.codex-global-state.json  JSONL over stdio
          │ unread IDs                    │ threads/status/events
          └──────────────┬─────────────────┘
                         ▼
                 Mac bridge :5180
              GET /api/v1/attention
                         │ Bearer token / LAN
                         ▼
         Waveshare ESP32-S3-Touch-AMOLED-2.06
            410×502 LVGL touch-scrollable inbox
```

The bridge never edits Codex's state files. Pin data primarily comes from the
App Server's built-in `Pinned` thread section; the persisted-state parser is a
compatibility fallback.

## 1. Start the Mac bridge

Prerequisites:

- macOS with Codex Desktop or Codex CLI installed;
- Codex CLI available on `PATH`, or Codex bundled inside `ChatGPT.app`/legacy `Codex.app`;
- Node.js 18.18 or newer.

```bash
git clone https://github.com/seichris/codex-attention-display.git
cd codex-attention-display/bridge
npm run setup
npm start
```

`npm run setup` creates `bridge/config.json` with a random 256-bit bearer token
and prints candidate LAN URLs. It refuses to overwrite an existing config.

Open the browser dashboard on the Mac:

```text
http://127.0.0.1:5180/
```

The ESP32 endpoint is:

```text
http://<mac-lan-ip>:5180/api/v1/attention
```

On macOS, a typical Wi-Fi address can be printed with:

```bash
ipconfig getifaddr en0
```

Allow incoming Node connections if the macOS firewall asks. The bridge logs no
prompt contents and does not expose full thread transcripts.

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

The bridge automatically checks the shell `PATH`, `~/.local/bin`, and the bundled
Codex binary inside `ChatGPT.app` or legacy `Codex.app`. An explicit `codexBin`
continues to win.

Environment variables can override the important values:
`CODEX_ATTENTION_CONFIG`, `CODEX_ATTENTION_HOST`,
`CODEX_ATTENTION_PORT`, `CODEX_ATTENTION_TOKEN`,
`CODEX_ATTENTION_POLL_MS`, `CODEX_BIN`, and `CODEX_HOME`.

### Run the bridge at login

From the repository root, install the included per-user LaunchAgent:

```bash
./scripts/install-macos-launch-agent.sh
```

The installer resolves the current Node and Codex binaries to absolute paths,
then starts `bridge/src/index.mjs` at login. Logs are written under
`~/Library/Logs/CodexAttentionDisplay/`. Remove it with:

```bash
./scripts/uninstall-macos-launch-agent.sh
```

## 2. Flash the Waveshare 2.06-inch board

Install and export ESP-IDF 5.4 or newer, then:

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig
```

Under **Codex Attention Display**, set:

1. Wi-Fi SSID and password;
2. the full bridge URL printed during setup;
3. the same bearer token generated for the bridge.

Then build and flash:

```bash
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

The first build downloads the official
`waveshare/esp32_s3_touch_amoled_2_06` BSP and LVGL through the ESP-IDF
Component Manager. The checked-in defaults mirror Waveshare's current LVGL example: a conservative
16 MB flash map on the board's 32 MB physical flash, plus 8 MB OPI PSRAM.

## Attention rules

The bridge emits a card when this predicate is true:

```text
waiting_for_approval
OR waiting_for_user_input
OR unread
OR pinned
```

Order:

1. waiting for approval;
2. waiting for user input;
3. newly completed and unread while this bridge was running;
4. any other unread thread;
5. pinned thread;
6. newest update first within a group.

“New” is intentionally conservative. After a bridge restart, an unread result
still appears as **UNREAD**, but only a completion observed live by this bridge
gets the **NEW** badge. Codex's persisted unread set does not record why a thread
became unread, so the bridge does not pretend it can always distinguish an
automatic unread result from “mark unread for later.”

## API

```bash
curl \
  -H "Authorization: Bearer <token>" \
  http://127.0.0.1:5180/api/v1/attention
```

Example response:

```json
{
  "version": 1,
  "count": 2,
  "totalCount": 2,
  "truncated": false,
  "items": [
    {
      "id": "019a…",
      "title": "Fix offline-map transfer",
      "project": "open-bike-computer",
      "status": "waiting_approval",
      "unread": true,
      "pinned": false,
      "newResult": false,
      "ageSeconds": 42,
      "reasons": ["waiting_approval", "unread"]
    }
  ]
}
```

See [docs/protocol.md](docs/protocol.md) for the complete device contract and
[docs/architecture.md](docs/architecture.md) for source-of-truth and failure
behavior.

## Security and compatibility notes

- The bearer token prevents accidental access but ordinary HTTP does not hide
  traffic from someone who can sniff the LAN. Use a trusted/private network.
- Unread state currently comes from Codex Desktop's local
  `.codex-global-state.json`. That is an internal, version-sensitive format.
  Parsing is deliberately read-only, recursive, and failure-tolerant.
- The current pin and detailed status integration uses App Server's experimental
  thread metadata. Older Codex versions may fall back to persisted pin IDs and
  may expose less precise status.
- If unread state cannot be read, the display says so instead of silently
  presenting the list as complete.

## Development

```bash
cd bridge
npm test
npm run check
```

Firmware CI uses Espressif's official ESP-IDF CI action. Local firmware builds
use normal `idf.py` commands.

## License

MIT. See [NOTICE](NOTICE) for trademark and affiliation information.
