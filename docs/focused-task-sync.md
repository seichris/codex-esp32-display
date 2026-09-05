# Focused task synchronization investigation

## Local event implementation — 2026-09-05

The companion now observes `thread-stream-following-changed` version 1 over the
existing `~/.codex/ipc/ipc.sock`. The tested Codex renderer sends this event when
its task view is presented or removed, and announces existing followed tasks to
new clients. This is a private, version-checked interface, not a documented public
active-window API. No Accessibility permission is needed for this detector. The companion also
prefers the running Desktop app’s bundled CLI for metadata, avoiding an older
PATH CLI that cannot interpret the same task storage.

`TaskEventConnection` verifies socket/directory ownership and permissions, the
same-user peer UID, and the broker PID's `com.openai.codex` bundle identity. It
uses bounded nonblocking I/O, a two-MiB frame cap, a two-second initialization
acknowledgement deadline, recurring idempotent initialization for liveness, and
bounded reconnect backoff. It never subscribes to task contents, claims task
ownership, or sends turn-control methods. Unknown or malformed selection events
invalidate the connection. Only operational counts/reasons are logged.

`TaskEventState` tracks exact task and host IDs per source client. Leave events
and client disconnections remove only that client's matching state. Paired
leave/enter events settle before confirmation. Zero candidates yields no task;
multiple presented targets (including the same task in two clients) yield
unavailable, instead of choosing whichever event arrived last. A remote target
is unsupported and cannot resolve against a coincident local ID. Disconnects
clear selection; main-thread observations still expire if updates stop.

This intentionally replaces the earlier focused-window promise with an
unambiguous-open-task contract. Multiple windows and auxiliary task views need
further physical qualification; the event itself does not identify the most
recently focused native window. Other same-user clients can also participate in
this private broker, so source IDs are tracked rather than treated as window IDs.

### Validation

- Read-only probe: two A → B → A round trips matched exact IDs and following
  booleans. Reconnect announced the already-open task. A later leave event matched
  the user's explicit confirmation of manual navigation away.
- Installed signed companion: operational log changed from `shell-document` to
  `confirmed`; authenticated bridge state and attention payload resolved exact IDs
  and titles for “Codex voice” and “Codex voice (2)”.
- 25 macOS tests passed, including fragmented/coalesced frames, invalid sizes,
  incompatible selection events, source isolation, ambiguous views, disconnects,
  remote hosts, task switching, non-task state, and existing dictation behavior.
- 26 bridge tests passed. Signed release companion and ESP-IDF 5.4.4 firmware
  builds passed. Existing firmware protocol is unchanged.
- Firmware was flashed to the connected ESP32-S3 and esptool verified the written
  data. Display behavior after the required power cycle is a separate user check.

`trusted` in new event diagnostic rows means the local broker peer was verified;
in historical Accessibility rows it meant AX permission was granted.

## Earlier Accessibility findings on 2026-09-05

The earlier Accessibility implementation did **not** detect tasks on the tested Codex build.
The installed companion contains both dictation and the observer; installing the
combined code was not sufficient to make the document-URL assumption valid.

- Verified the running companion against the signed local build and verified
  that it contains `FocusedTaskObserver`, `FocusedTaskSelection`,
  `DictationRecorder` and `DictationModel`.
- Accessibility originally appeared enabled in System Settings while the running
  companion reported untrusted. A bundle-scoped permission reset and regrant
  fixed that mismatch. Rebuilding with an ad hoc signature reproduced it: the
  designated requirement was the binary's changing `cdhash`. Local builds now
  support an Apple Development signing identity with a stable designated
  requirement. The identity selection is local and ignored by Git.
- With permission granted, operational logs showed one Codex process, successful
  AX reads, eight structural nodes, one web area and one app-origin document
  candidate. Reads took about 1–17 ms. Electron's documented
  `AXManualAccessibility` setup returned `-25205` (attribute unsupported) on this
  installation; the subsequent metadata reads still succeeded.
- The installed `/Applications/ChatGPT.app` renderer
  `webview/assets/app-initial-7a6c8787453d.js` uses a memory-history router
  (`Mvn` / `UDo`). The main-process bundle
  `.vite/build/window-all-closed-D6_N7yxp.js` constructs
  `app://-/index.html` with an optional `initialRoute` parameter.
- In the renderer's `navigate-to-route` handler, `initialRoute` is updated only
  when `persistForReload` is true. Ordinary in-memory navigation does not make
  the top-level document URL an authoritative current-task route. Reading
  `initialRoute` would risk stale confirmation and is explicitly rejected.
- The app still emits an internal active-task event. Static evidence of that
  event is not evidence of an externally supported subscription. No supported
  current-desktop-selection endpoint was identified in the public app-server
  interface during this investigation. This is not a claim that such an
  integration is impossible in principle.

The second certificate-signed update retained Accessibility permission without
another reset. Its live log reports `trusted: true`, `reason: shell-document`,
one web area and one candidate. The authenticated bridge is connected and has
no current task; dictation readiness remains true. The final build passes all
16 macOS tests and signature verification. No firmware was changed or flashed.

The companion now distinguishes missing permission, missing windows, AX read
errors, bounded-scan limits, missing or ambiguous documents, and the bootstrap
app document. It keeps the device's selection unavailable when only the app
shell is exposed. This prevents false confirmation; it does not implement a new
source of live task selection.

### Diagnostic logs

`~/Library/Logs/CodexESP32Display/focused-task.log` records timestamp, reason,
process count/PID, trust status, the accessibility-setup result when attempted,
AX error codes keyed by attribute, visited-node/web-area/candidate counts and
elapsed milliseconds. It never records raw URLs, task IDs, window titles,
transcripts, clipboard contents or credentials. Changes are logged immediately;
unchanged state is logged at most every 30 seconds. Files are mode 0600 with a
256 KiB rotation threshold and one previous file.

Voice Settings includes **Show Detection Log**, and reopening the menu-bar app
opens Voice Settings. To qualify a future selection source, switch between two
known tasks and verify exact identity changes through the authenticated bridge;
then test settings/new-task pages, multiple windows, stale reads and unsupported
hosts. A working window read alone is not a successful selection test.

### Sources

- [Electron accessibility setup](https://www.electronjs.org/docs/latest/tutorial/accessibility)
- [Apple code-signing requirements and privacy identity](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements)
- [Apple bounded AX attribute reads](https://developer.apple.com/documentation/applicationservices/1462060-axuielementcopyattributevalues)
- [Public Codex app-server protocol](https://github.com/openai/codex/blob/main/codex-rs/app-server/README.md)

## Earlier design and evidence

## Intended behavior

The fixed bottom card follows the task open in the most recently focused Codex
main window, including tasks opened manually and tasks outside the attention
list. With multiple windows, changing the focused Codex window changes the card.
Switching to another Mac app does not itself select a different Codex task.
Settings, a new-task page, closing the window, and an unreadable selection must
not leave a previous task labeled as confirmed current.

## Earlier source inspection, 2026-09-05

Inspected installed Codex version 26.803.41515, build 6321, without modifying it.

- The renderer bundle `webview/assets/app-initial-Biw83Aiz.js` stores sidebar
  task ID, host ID, task kind, and selected state in separate `data-app-action-*`
  attributes. Titles are not necessary to identify tasks inside the renderer.
- It emits `remote-hosted-pip-active-thread-changed` with a `conversationId`.
  `.vite/build/main-DJC9FKq9.js` handles that message through
  `setRemoteHostedPIPActiveThreadID` after checking the trusted Electron IPC
  sender. This is an internal renderer-to-main-process event, not a demonstrated
  subscription endpoint for an external companion.
- The current `.codex-global-state.json` has saved task and workspace state,
  but inspection did not find an authoritative current-task field. Per-task
  panel routes and recent activity must not be used as current selection.
- `DesktopVoiceController` currently remembers device-requested focus and calls
  it inferred. It does not observe manual navigation.
- The bridge already publishes `currentThread` and the flashed UI already maps
  `focusConfidence: confirmed` to `CURRENT ON MAC`.
- Live Accessibility verification is incomplete: the available computer-use
  tool explicitly refused to inspect Codex for safety reasons. No alternate
  UI-inspection route was used to bypass that restriction.

Internal DOM attributes do not prove that those attributes or exact task IDs
are exposed through macOS Accessibility. That is the first feasibility gate.

## Recommended implementation sequence

1. Validate an exact-ID observation source before wiring it to the device.
   Prefer a supported Codex selection API/event if one becomes available.
   Otherwise evaluate an opt-in macOS Accessibility observer in the companion:
   the focused window's document URL or selected task link must expose an exact
   task ID. Title matching is insufficient. If neither exact ID nor an
   unambiguous host can be obtained, report unavailable instead of guessing.
2. Add a `FocusedTaskObserver` independent of voice/dictation control. Publish a
   value containing availability, task ID, host ID, kind, window identity, source,
   observation time, and whether Codex is frontmost. Model known non-task pages
   separately from failures to observe. Use window/focus/navigation notifications
   plus a bounded refresh to recover missed changes. Expire stale observations.
3. Separate `observedTask`, `requestedFocusTarget`, and `dictationTarget`.
   A deep-link request is pending/inferred until observation confirms it. An
   active dictation session remains bound to the ID captured when it started.
   Never move that recording to a new task merely because the user changed tabs.
4. Extend the existing private companion IPC state payload with observation
   metadata. Keep old fields compatible. The bridge derives `currentThread`
   from the observed selection and resolves its title/project by exact ID even
   if it is unpinned, read, old, or absent from the attention list.
5. Carry host/kind through resolution. The current bridge primarily reads local
   Codex tasks; remote-host tasks and ChatGPT conversations must not be resolved
   against the local task cache. Initially report unsupported targets explicitly
   until those backends are implemented.
6. Use the existing `currentThread` / confirmed-focus firmware path for the
   basic card. Additional labels for background, stale, unsupported, and
   dictation-target states may require a later firmware change and approved flash.

Do not conflate voice state for a previous dictation target with the observed
current task. Only attach session status when the target identities agree.

## Acceptance checks

- Manually open task A, then B: exact IDs and card titles follow within the
  observation-plus-bridge polling budget; target about two seconds initially.
- Open a task absent from pinned/unread results: the card still resolves it.
- Two tasks with the same title: the correct ID is retained.
- Switch between two Codex windows, navigate back/forward, close a window, open
  settings/new task, and quit/relaunch Codex: no stale confirmed task.
- Background Codex: preserve a still-observed open task; do not infer a new
  selection from unrelated Mac activity.
- Revoke Accessibility or simulate observer timeout: report unavailable and
  prevent commands based on stale confirmation.
- Switch task while recording: the card follows the observed task while the
  recording retains its original destination.
- Select remote/ChatGPT content: resolve the correct backend or report it as
  unsupported, never show a coincident local task.

## Initial observer implementation at 4be9bad (historical)

The companion now includes `FocusedTaskObserver`, a read-only macOS Accessibility
adapter, and a strict `FocusedTaskSelection` policy. It observes the focused/main
window document URL or a single shallow root web-area URL. Only the trusted
`app://-/local/<UUID>` origin/path with a local host is confirmed. It never scans
transcript text, matches titles, changes focus, or reads the clipboard.

The observer polls once per second, refreshes on application activation/exit,
uses a background queue with bounded AX timeouts/traversal, and expires readings
after three seconds. Non-task routes, unsupported hosts, ambiguous results and
permission failures clear the confirmed ID. Voice Settings exposes detection
status and a user-operated Accessibility permission button.

Companion `state` responses now use the observation, independently of explicit
focus/voice command acknowledgements. The requested target and dictation session
retain their existing separate identities. The bridge's existing exact-ID read
path resolves selected tasks outside the attention list; its integration test now
covers old/unpinned task selection, manual switching, and losing selection.

The macOS policy tests cover strict route/origin parsing, unsupported/ambiguous
hosts, non-task navigation, stale observations, and voice-state target isolation.
These are contract tests, not proof that Codex exposes its navigation URL through
Accessibility. The observer has not been launched here to work around the UI
tool's explicit Codex restriction. Live qualification must still establish that
main-window AXDocument/AXURL changes with manual navigation. If it does not, this
adapter correctly stays unavailable; another supported observation interface is
needed before automatic synchronization can be claimed.

No firmware change is required for the basic confirmed-task card. No device flash
or companion installation was performed for this observer implementation.
