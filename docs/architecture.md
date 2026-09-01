# Architecture

## Goal

The display is a physical inbox, not a process monitor. Its list is the union of
threads that are waiting, unread, or pinned. A selected thread can be opened to
read its latest useful text without marking it read.

## Sources of truth

| Signal | Primary source | Fallback | Mutated? |
|---|---|---|---:|
| Thread metadata | `thread/list` | Last in-memory successful page | No |
| Waiting for approval | `ThreadStatus.activeFlags.waitingOnApproval` | None | No |
| Waiting for user input | `ThreadStatus.activeFlags.waitingOnUserInput` | None | No |
| Pinned | Built-in pinned thread section | Recognized persisted pin-ID keys | No |
| Unread / manually unread | Recognized IDs in `.codex-global-state.json` | None | No |
| Newly completed | Live `turn/completed` intersected with unread | Plain unread after restart | No |
| Latest text | `thread/turns/list`, full items, newest first | `thread/read(includeTurns=true)` | No |

## Bridge components

### `CodexAppServerClient`

- starts `codex app-server --listen stdio://`;
- performs `initialize` → `initialized`;
- paginates `thread/list`;
- reads recent full turn items only when a detail view is requested;
- falls back to `thread/read` for older App Server versions;
- never logs or publishes full transcripts except to the authenticated caller
  requesting one current attention thread.

### `DesktopStateReader`

- reads only `~/.codex/.codex-global-state.json`;
- recognizes current and legacy unread/pinned key shapes;
- retains the last good IDs across a transient parse failure;
- never writes the file.

### `CodexAttentionService`

- merges sources, ranks cards, and truncates the device payload;
- caches metadata and short-lived latest-text results;
- only serves detail for IDs in the current attention payload;
- reconnects App Server and preserves the last good list.

### HTTP bridge

- `GET /` — browser list/detail preview;
- `GET /healthz` — public health without thread content;
- `GET /api/v1/attention` — authenticated list;
- `GET /api/v1/threads/:id/latest` — authenticated, bounded latest text;
- `POST /api/v1/refresh` — authenticated forced refresh.

## Firmware components

- `wifi_manager`: Wi-Fi connection and retry state.
- `attention_client`: authenticated list/detail HTTP client with a 64 KiB hard
  response ceiling.
- `attention_ui`: LVGL list, persistent selection, and scrollable detail view.
- `button_input`: debounced BOOT/GPIO0 plus AXP2101 PWR short-press polling.
- `main`: independent list polling, detail fetching, and button tasks.

All LVGL mutation is protected by the Waveshare BSP display lock. Detail HTTP
reads do not run in the LVGL task. A returned detail is rendered only if its ID
still matches the thread currently open, preventing stale network responses from
replacing a newer selection.

## Physical controls

```text
BOOT short  -> next highlighted item
PWR short   -> open highlighted item / return from detail
BOOT detail -> next item and open it
Touch       -> scroll; tap a card to open
```

The AXP2101 integration enables and clears only the PKEY short-press status bit.
It does not alter the PMIC's long-hold shutdown configuration.

## Failure behavior

- Codex unavailable: cached list remains and diagnostics report the error.
- Desktop state unreadable: waiting and pin signals continue; unread availability
  is shown as degraded.
- Wi-Fi down: last list remains and firmware reports connection failure.
- Detail failure: the current thread stays selected and the detail screen shows
  an error; PWR returns to the list.
- Thread removed while open: the next list refresh safely returns to the list.
- Oversized payload: bridge truncation plus firmware response/text caps bound
  memory use.
