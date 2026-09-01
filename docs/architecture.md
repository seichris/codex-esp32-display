# Architecture

## Goal

The display is a physical inbox, not a process monitor. Its output is the union
of threads that are waiting, unread, or pinned.

## Sources of truth

| Signal | Primary source | Fallback | Mutated? |
|---|---|---|---:|
| Thread ID, name, preview, cwd, updated time | `thread/list` from `codex app-server` | Last in-memory successful page | No |
| Waiting for approval | `ThreadStatus.activeFlags` → `waitingOnApproval` | None | No |
| Waiting for user input | `ThreadStatus.activeFlags` → `waitingOnUserInput` | None | No |
| Pinned | App Server thread section with the built-in pinned section ID/name | Recognized persisted pin-ID keys | No |
| Unread / manually marked unread | Recognized unread-ID keys in `.codex-global-state.json` | None | No |
| Newly completed | Live `turn/completed` notification, intersected with unread | Falls back to plain unread after restart | No |

The bridge requests App Server's experimental API because thread sections are
currently experimental. Unknown fields are ignored and absent fields degrade to
fallback behavior.

## Components

### `CodexAppServerClient`

- spawns `codex app-server --listen stdio://`;
- performs the required one-time `initialize` → `initialized` handshake;
- maintains request IDs and timeouts;
- paginates `thread/list` up to a configured cap;
- emits notifications to the attention service;
- returns a method-not-found error for unexpected server-initiated requests so
  an observer process cannot deadlock App Server.

### `DesktopStateReader`

- reads only `~/.codex/.codex-global-state.json`;
- caches by file modification time;
- recognizes current and legacy unread/pinned key names;
- understands host-scoped objects, arrays, and JSON-encoded nested values;
- returns an explicit unavailable/error state rather than throwing through the
  service loop.

### `CodexAttentionService`

- merges App Server threads with Desktop state;
- records live completion timestamps;
- ranks and truncates cards;
- refreshes periodically and after relevant notifications;
- reconnects after App Server exits;
- retains the most recent thread page in memory during a transient failure.

### HTTP bridge

- `GET /` — human browser preview;
- `GET /healthz` — public process/connection health, no thread details;
- `GET /api/v1/attention` — token-protected device payload;
- `POST /api/v1/refresh` — token-protected forced refresh.

### Firmware

The firmware uses Waveshare's managed ESP-IDF BSP. Network fetches run in a
FreeRTOS task; LVGL mutation is wrapped in the BSP display lock. The device keeps
no Codex credentials, only the bridge URL and its LAN bearer token.

## Ranking

A single card may carry multiple reasons. Its primary rank is:

```text
0  waiting approval
1  waiting user input
2  live-observed completion + unread
3  unread
4  pinned
```

Within equal rank, most recently updated threads come first.

## Failure behavior

- **Codex process unavailable:** the bridge reconnects; cached cards remain, and
  the payload exposes `diagnostics.sourceError`.
- **Desktop state unreadable:** waiting and App-Server pin signals continue, but
  the payload sets `desktopStateAvailable=false` and the device warns that unread
  state is unavailable.
- **Bad device token:** the bridge returns HTTP 401; firmware displays a request
  error without printing the secret.
- **Wi-Fi down:** the board keeps retrying and shows a Wi-Fi status error.
- **Payload too large:** bridge-side card limits and a 64 KiB firmware response
  ceiling prevent unbounded allocation.
