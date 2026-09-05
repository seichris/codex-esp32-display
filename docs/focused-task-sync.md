# Focused task synchronization investigation

## Intended behavior

The fixed bottom card follows the task open in the most recently focused Codex
main window, including tasks opened manually and tasks outside the attention
list. With multiple windows, changing the focused Codex window changes the card.
Switching to another Mac app does not itself select a different Codex task.
Settings, a new-task page, closing the window, and an unreadable selection must
not leave a previous task labeled as confirmed current.

## Verified evidence, 2026-09-05

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

## Implementation and delivery status

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
