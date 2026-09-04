# Desktop Codex Voice implementation plan

## Outcome

Turn the Waveshare ESP32-S3-Touch-AMOLED-2.06 into a USB microphone and
physical controller for Codex Desktop Voice.

The intended interaction is:

1. Select a thread on the inbox list, or remain in its detail view.
2. Hold either physical button for one second.
3. The Mac focuses that exact Codex thread, starts or resumes Desktop Voice,
   and the Waveshare microphones become live.
4. Hold either physical button for one second again to mute the microphone.
   The Voice Chat remains open so its reply is not cut off.
5. Hold either button again to resume the microphone. Ending the Voice Chat
   remains an explicit action in Codex Desktop for the first version.

The list also gains a fixed bottom card for the current Desktop/Voice thread.
The first touch on that card focuses it without opening details. Touching the
same card again, without selecting another thread in between, opens its detail
view.

## Implementation status

Implemented on the `desktop-codex-voice` branch:

- authenticated bridge endpoints for Desktop state, focus, and Voice control;
- a private per-launch bridge-to-companion control channel;
- configurable thread URL template and Voice keyboard shortcut on macOS;
- ESP32-S3 USB Audio Class microphone streaming at 48 kHz mono/16-bit;
- fail-closed live/silence gating, with mute controlled locally on the device;
- one-second long-press events for BOOT and AXP2101 PWR;
- the fixed current/target thread card and two-tap focus/detail interaction.

The source-level implementation and automated builds/tests do not prove four
physical/interactive behaviors: USB audio enumeration and sample quality on the
actual board, the Codex Desktop deep link selecting the exact task, the
configured Voice shortcut opening Voice in that task, or the exact AXP2101 PWR
long-press behavior on the target board revision. Those remain acceptance gates
before calling the feature production-ready.

## Feasibility summary

The complete experience is feasible, but several parts require short physical and
Desktop-integration checks before it can be considered production-ready.

| Capability | Assessment | Evidence or required proof |
| --- | --- | --- |
| Capture the board microphones | Implemented; needs a physical audio check | The Waveshare BSP exposes `bsp_audio_codec_microphone_init()` for the ES7210 input codec. |
| Appear as a macOS microphone | Implemented; needs USB enumeration and audio checks | The firmware uses Espressif's USB Audio Class device component with macOS compatibility enabled. |
| Start Desktop Voice | Implemented with a configured Voice hotkey; needs an interactive check | OpenAI documents a configurable Voice Chat hotkey. Generic-device start/stop control is not otherwise documented. |
| Toggle microphone capture | Implemented; needs a physical privacy check | The USB microphone stays enumerated and sends live PCM only in the listening state; otherwise it sends silence. |
| One-second BOOT hold | Implemented; needs a physical check | BOOT is GPIO0 and its live level is polled and debounced. |
| One-second PWR hold | Implemented; needs a physical check | The AXP2101 one-second long-press IRQ is enabled independently of its longer hardware shutdown threshold. |
| Fixed current-thread card and two-stage touch | Implemented | This is entirely within the LVGL UI and device protocol. |
| Determine and focus the exact current Desktop task | Partially implemented | The installed Codex build contains the `codex://threads/{id}` route, but App Server does not expose the window's selected task. The companion therefore labels URL-based focus as `inferred`, never `confirmed`. |

Therefore the project should proceed through the feasibility gates below. Do
not represent a thread as "current on Mac" or unmute the microphone for a newly
selected thread until the Mac has positively acknowledged that focus request.

## Product semantics

### Recording state

"Recording" means that microphone PCM from the ES7210 is being delivered to
the macOS USB audio input while a Desktop Voice Chat is active. The firmware
does not retain recordings.

The user-visible states are:

```text
OFFLINE -> READY -> FOCUSING -> STARTING -> LISTENING -> MUTED
                    |             |             |
                    +-----------> ERROR <-------+
```

- `READY`: USB audio is connected, but the device is sending silence.
- `FOCUSING`: the Mac is switching to the selected thread.
- `STARTING`: the Mac is invoking the configured Voice Chat hotkey.
- `LISTENING`: live microphone samples are flowing.
- `MUTED`: the Voice Chat may still be active, but the device sends silence.
- `ERROR`: focus, Desktop Voice, USB, microphone, or bridge state could not be
  confirmed. The device must fail closed to silence.

The screen must display a persistent, unambiguous listening indicator in both
list and detail views. A transient toast is insufficient for microphone privacy.

### Physical-button behavior

Both BOOT and PWR should produce semantic button events rather than their
current immediate actions:

```text
PRESS
RELEASE_SHORT
LONG_PRESS_1S
RELEASE_AFTER_LONG
```

- Emit `LONG_PRESS_1S` once when the one-second threshold is crossed; do not
  repeat while the button remains down.
- A long press consumes the gesture. Its eventual release must not also perform
  the button's short-press action.
- Existing short-press behavior remains unchanged.
- The voice gesture is accepted in the list only when a thread is selected.
- In detail view, the visible detail thread is the voice target.
- When `LISTENING`, either button's long press immediately returns audio to
  silence before any network round trip, then reports the new state to the Mac.
- When not listening, the device requests thread focus first and does not
  unmute until the Mac acknowledges the correct thread.

Current short-press mappings remain:

| Context | BOOT short | PWR short |
| --- | --- | --- |
| List | Select next item | Open selected item |
| Detail | Select and open next item | Return to list |

### Fixed current-thread card

The bottom card is outside the scroll container and remains visible while the
attention list scrolls.

- Label it `CURRENT ON MAC` only when Desktop focus is confirmed.
- Label it `VOICE TARGET` when it is merely the bridge's last requested target.
- Never display an inferred target as confirmed.
- Include title, project, thread status, and Voice state.
- If the current thread is also in the attention inbox, omit its duplicate from
  the scrolling list.
- If no current thread can be determined, show a disabled `NO CURRENT THREAD`
  card rather than a stale thread.
- The fixed card participates in physical-button selection after the final
  scrolling item.

Touching the fixed card uses a two-stage, stateful gesture rather than a timed
double-tap:

1. The first touch selects the card and requests/refreshes Desktop focus. It
   never opens details.
2. A later touch on that same still-selected card opens details.
3. Selecting another thread, leaving the list, changing the fixed thread, or a
   failed focus request clears the armed second-touch state.

Ordinary scrolling cards keep their current one-touch-open behavior. Physical
PWR short press also keeps opening the selected card.

## Architecture

```text
ES7210 microphones
       |
       | I2S, mono PCM16
       v
ESP32 audio service ----> USB Audio Class microphone ----> macOS Core Audio
       |                                                    |
       | silence/live gate                                  v
       |                                             ChatGPT Desktop Voice
       |
       +-- long-press/focus commands over authenticated LAN HTTP
                                      |
                                      v
                              existing Node bridge
                                      |
                         private local IPC, loopback only
                                      |
                                      v
                         macOS DesktopVoiceController
                          | focus task | invoke hotkey
                          +------------+--------------------> ChatGPT Desktop

Codex App Server + read-only Desktop state
       |
       +--> attention list + confirmed/inferred current thread --> ESP32 UI
```

USB carries audio. The existing authenticated LAN bridge remains the control
and thread-state channel. This keeps USB descriptors focused on standards-based
audio and avoids pretending to be OpenAI's supported Codex Micro hardware.

The Swift menu-bar companion should own Desktop automation. The Node bridge
must communicate with it through a loopback-only authenticated IPC channel.
When the companion is not running, read-only inbox behavior continues and
voice/focus endpoints return a clear `desktop_control_unavailable` result.

## Phase 0: feasibility gates

Complete these spikes first and record the results in this document or a linked
test report. Stop the feature if a gate has no safe supported implementation.

### Gate A: USB microphone enumeration and audio quality

1. Confirm which board USB-C connector reaches the ESP32-S3 native USB pins and
   whether enabling TinyUSB device mode conflicts with the current USB
   Serial/JTAG console used for flashing and logs.
2. Add a disposable UAC 1 microphone prototype using mono, signed PCM16 at
   48 kHz. Keep flashing recovery available even if the application USB
   descriptor is invalid.
3. Initialize the ES7210 through `bsp_audio_codec_microphone_init()` and record
   at least 60 seconds in macOS.
4. Measure clipping, DC offset, dropped frames, sustained endpoint underruns,
   and usable speech level from both physical microphone positions.
5. Confirm the device appears in macOS Sound settings and is accepted by
   ChatGPT's microphone permission flow.

Pass condition: macOS records intelligible, stable speech for 30 minutes with
no USB resets and with a recoverable firmware flashing path.

### Gate B: Desktop Voice control

1. Configure a unique shortcut in ChatGPT Settings > Voice > Voice chat hotkey.
2. Verify whether the shortcut starts Voice in the currently open Codex task
   when ChatGPT is foregrounded and when it is backgrounded.
3. Verify what the shortcut does while Voice is already active. Do not assume
   that it mutes, stops, or toggles the session unless observed.
4. Prototype invocation from the Swift companion with a macOS `CGEvent` and
   document the required Accessibility/Input Monitoring permissions.
5. Verify that audio can remain muted at the ESP32 source while ChatGPT speaks,
   then resume without USB re-enumeration or a new Voice Chat.

Pass condition: one authenticated bridge request can reliably start/resume the
correct task's Voice Chat, and local PCM gating can mute/resume input without
ending the reply.

### Gate C: current-thread observation and focus

Try integration surfaces in this order:

1. A documented or accepted `codex:` deep link that addresses an exact thread
   ID and is stable across app restarts.
2. A stable macOS Accessibility element or action with a non-localized
   identifier for the task/sidebar selection.
3. Read-only observation of Desktop's persisted state, combined with an
   Accessibility focus action. Internal state must never be written.

Do not emulate another device's vendor/product identity or private protocol.
Do not select a task by title alone because titles are not unique.

If exact Desktop observation is unavailable but focusing by thread ID works,
the bridge may expose the last successfully focused device target as
`inferred`. If neither focus nor observation is reliable, retain the fixed card
as a device-only `VOICE TARGET`, require the user to open the task manually,
and block automatic Voice start rather than risk the wrong task.

Pass condition: the Mac can acknowledge the exact requested thread ID and the
device updates when the Desktop task changes independently. If only the first
half passes, ship the row as `VOICE TARGET`, not `CURRENT ON MAC`.

### Gate D: AXP2101 PWR hold

1. Identify the exact AXP2101 press, release, short-press, and long-press status
   bits for this board revision.
2. Observe register transitions during 0.2, 0.8, 1.0, 1.2, and 2.0 second holds
   without changing the PMIC's shutdown configuration.
3. Confirm that a one-second application hold neither powers down the board nor
   delays the existing PWR short-press event unacceptably.
4. Implement a one-event-per-hold decoder only after the physical evidence is
   known.

Pass condition: PWR can distinguish a one-second hold from a short press and
from hardware shutdown on the exact device. If it cannot, support the voice
gesture on BOOT and touch only and document the hardware limitation; do not
silently redefine a PMIC power gesture.

## Phase 1: device protocol and bridge state

Extend protocol v1 additively; existing firmware should continue ignoring new
fields.

Add a top-level `currentThread` object to the attention payload:

```json
{
  "currentThread": {
    "id": "opaque-thread-id",
    "title": "Desktop Voice implementation",
    "project": "codex-esp32-display",
    "status": "idle",
    "focusConfidence": "confirmed",
    "voiceState": "muted"
  },
  "capabilities": {
    "desktopFocus": true,
    "desktopVoiceHotkey": true,
    "powerButtonLongPress": true
  }
}
```

`focusConfidence` is `confirmed`, `inferred`, or `unavailable`. `voiceState` is
`ready`, `focusing`, `starting`, `listening`, `muted`, `error`, or `unknown`.

Add authenticated endpoints:

- `POST /api/v1/desktop/focus` with `{ "threadId", "requestId" }`.
- `POST /api/v1/desktop/voice` with
  `{ "threadId", "command": "start-or-resume" | "mute" }`.
- `GET /api/v1/desktop/state` for reconciliation after reconnects.

Requirements:

- Reuse bearer authentication and constant-time token comparison.
- Accept JSON only, impose a small fixed request-body limit, validate opaque
  thread IDs, and reject unknown fields/commands.
- Make commands idempotent through `requestId` and return the acknowledged
  thread ID and resulting confidence/state.
- Serialize focus and Voice commands so refresh polling cannot reorder them.
- Never log transcript content, microphone samples, bearer tokens, or complete
  Desktop-state files.
- Preserve the bridge's current read-only App Server behavior; Desktop control
  belongs in the explicit controller path, not in `turn/start`.

Likely files:

- `bridge/src/desktop-state.mjs`
- `bridge/src/service.mjs`
- `bridge/src/http-server.mjs`
- a new `bridge/src/desktop-controller.mjs`
- `bridge/test/desktop-state.test.mjs`
- `bridge/test/http-server.test.mjs`
- `bridge/test/service.integration.test.mjs`
- `docs/protocol.md`
- `docs/architecture.md`

## Phase 2: macOS Desktop Voice controller

Add a `DesktopVoiceController` to the existing Swift menu-bar companion.

Responsibilities:

- Watch the supported Desktop focus signal established by Gate C.
- Resolve only exact thread IDs returned by the bridge/App Server.
- Focus ChatGPT Desktop on a requested thread and acknowledge the resulting ID.
- Invoke the user-configured Voice Chat hotkey.
- Report whether focus and Voice state are confirmed or only inferred.
- Expose a private IPC service only to the bundled bridge child process.
- Fail closed when ChatGPT, Voice availability, Accessibility permission, or
  the configured hotkey is unavailable.

Add a setup panel that checks:

- ChatGPT Desktop is installed and running.
- Voice is available for the current account/workspace.
- macOS microphone permission is granted to ChatGPT.
- Accessibility/Input Monitoring permission is granted where needed.
- The Voice Chat hotkey is configured and matches the companion setting.
- The Waveshare UAC microphone is connected.

Do not claim to detect Voice state from button requests alone. If ChatGPT does
not expose a stable observable state, return `unknown` and let the ESP32's local
audio gate remain the privacy source of truth.

Likely files:

- `macos/Sources/CodexESP32Display/BridgeController.swift`
- `macos/Sources/CodexESP32Display/MenuBarView.swift`
- new `macos/Sources/CodexESP32Display/DesktopVoiceController.swift`
- new focused tests under `macos/Tests/`
- `macos/README.md`

## Phase 3: firmware USB audio service

Create a single audio subsystem shared by microphone capture and the existing
attention chime.

1. Replace or supplement the host-oriented USB dependency with the appropriate
   ESP-IDF TinyUSB device component.
2. Add UAC 1 descriptors for one mono PCM16 microphone at 48 kHz. Use a stable,
   project-owned USB identity and descriptive product string.
3. Initialize the BSP I2S interface explicitly at 48 kHz duplex before either
   speaker or microphone handles are created. The current speaker-first path
   implicitly initializes shared I2S at 22.05 kHz and must not race a 48 kHz
   microphone.
4. Open the ES7210 input codec and continuously drain I2S into bounded DMA/ring
   buffers.
5. Feed live samples to UAC in `LISTENING`; feed zero-filled frames in every
   other state. Never stop servicing the isochronous endpoint merely because
   the microphone is muted.
6. Convert the existing generated chime to the common 48 kHz clock and
   serialize codec access. Initially keep Desktop Voice output on the Mac to
   avoid acoustic echo through the device speaker.
7. Select or down-mix one clean microphone channel after Gate A. Beamforming,
   echo cancellation, and device-speaker Voice playback are out of scope.
8. Keep a hardware-recovery flashing path and document any loss of USB
   Serial/JTAG logging while UAC is active.

Suggested modules:

- new `firmware/main/voice_audio.c` and `voice_audio.h`
- new `firmware/main/usb_microphone.c` and `usb_microphone.h`
- refactor `firmware/main/attention_audio.c`
- update `firmware/main/idf_component.yml`
- update `firmware/main/CMakeLists.txt`
- update `firmware/sdkconfig.defaults`

## Phase 4: long-press and voice control state machine

Refactor `button_input` so it reports debounced press lifecycle events. Add a
small, pure state machine that can be unit tested independently from GPIO/I2C.

In `main.c`:

1. Resolve the voice target from the detail thread or selected list item.
2. On a long press while listening, close the PCM gate immediately and send a
   best-effort `mute` update.
3. On a long press while muted/ready, request exact thread focus.
4. Wait for a bounded acknowledgement containing the same thread ID.
5. Request `start-or-resume` and open the PCM gate only after success.
6. On timeout, mismatch, bridge loss, USB disconnect, or Desktop-control error,
   keep the gate closed and show a concise error.
7. Reconcile `/api/v1/desktop/state` after Wi-Fi or bridge reconnection without
   automatically opening the microphone.

The bridge request must run outside the LVGL lock. UI updates return through a
queue/event group so audio, HTTP, and touch handling cannot block one another.

Likely files:

- `firmware/main/button_input.c` and `button_input.h`
- `firmware/main/main.c`
- `firmware/main/attention_client.c` and `attention_client.h`
- new `firmware/main/voice_control.c` and `voice_control.h`

## Phase 5: fixed-card UI and selection model

Refactor `attention_ui.c` to separate three concepts that are currently coupled:

- scrolling attention index;
- selected thread ID;
- confirmed/inferred current Desktop thread ID.

Implementation outline:

1. Reserve approximately 100 pixels at the bottom for a fixed card and reduce
   the scrolling list's height accordingly.
2. Render the fixed card as a sibling of `s_list`, not as its child.
3. Use thread IDs, not array indices, for selection persistence across refreshes.
4. Build the physical selection ring from visible scrolling IDs followed by the
   fixed-card ID, with duplicates removed.
5. Give the fixed card its own `LV_EVENT_CLICKED` handler and armed-second-touch
   state.
6. Reset the arm state whenever the selected ID or current-thread ID changes.
7. Show listening/muted/focusing/error state in both views and keep it visible
   during detail scrolling.
8. Permit detail loading for the fixed thread even when it is outside the
   attention subset. The bridge's current latest-text authorization must be
   extended from "attention item only" to "attention item or current thread."

Likely files:

- `firmware/main/attention_model.h`
- `firmware/main/attention_ui.c` and `attention_ui.h`
- `firmware/main/attention_client.c`
- `bridge/src/service.mjs`
- `docs/protocol.md`

## Verification plan

### Automated checks

- Bridge unit tests for current-thread normalization, duplicate removal,
  command validation, authentication, request-size limits, idempotency, stale
  acknowledgements, and controller-unavailable behavior.
- Bridge integration tests for focus-before-voice ordering and reconnect state.
- Swift tests with a fake Desktop adapter and IPC client; no test should send a
  real global hotkey.
- Pure firmware tests for BOOT/PWR short-versus-long event sequences, one event
  per hold, release suppression, voice-state transitions, and selection-ring
  behavior.
- Existing `npm test`, `npm run check`, and ESP-IDF build remain green.
- CI performs a descriptor/build check but does not count as macOS enumeration,
  microphone, button, or Desktop Voice proof.

### Physical acceptance matrix

Test on the exact Waveshare board and current ChatGPT Desktop build:

| Scenario | Expected result |
| --- | --- |
| BOOT short in list/detail | Existing next-item behavior; no Voice action. |
| PWR short in list/detail | Existing open/back behavior; no Voice action. |
| BOOT one-second hold in list with selection | Correct task focused, Voice started/resumed, listening indicator shown. |
| BOOT one-second hold in detail | Visible detail task targeted. |
| PWR one-second hold in both views | Same result as BOOT, without shutdown or extra short action. |
| Either long hold while listening | PCM becomes silence locally before the network acknowledgement. |
| Fixed card first touch | Focus/select only; detail remains closed. |
| Fixed card second consecutive touch | Detail opens for that exact ID. |
| Desktop task changed with mouse | Not observable through the current public surface; the card remains an inferred `VOICE TARGET`, not `CURRENT ON MAC`. |
| Selected thread cannot be focused | Microphone remains silent; explicit error appears. |
| Bridge/Wi-Fi disappears while listening | Local microphone gate closes immediately. |
| USB cable is removed/reinserted | Clear status, safe silence, clean re-enumeration; no automatic unmute. |
| Voice unavailable or another Voice Chat active | Clear error; no wrong-task audio. |
| 30-minute conversation | No USB resets, audio starvation, UI lockup, or runaway heap use. |
| Attention chime during Voice | No sample-rate reset or corruption; chime policy may suppress sounds while listening. |

Record macOS audio-device evidence, serial logs, the exact firmware commit, and
the ChatGPT Desktop version. Physical acceptance is separate from source review,
local builds, and CI.

## Rollout order

1. Land the four feasibility results and architecture decision.
2. Land bridge protocol/state changes with the controller mocked.
3. Land the macOS controller and setup diagnostics.
4. Land USB microphone capture and the local privacy gate.
5. Land long-press behavior, initially enabled for BOOT only.
6. Enable PWR long press only after Gate D passes on hardware.
7. Land the fixed current-thread card and focus acknowledgement.
8. Run the physical acceptance matrix before enabling Voice by default.

## Non-goals for the first release

- Sending audio to Codex App Server or the OpenAI API.
- Recording or storing microphone audio.
- Voice playback through the Waveshare speaker.
- Beamforming, acoustic echo cancellation, or wake-word activation.
- Wireless microphone transport or a macOS virtual audio driver.
- Reverse-engineering or impersonating Codex Micro hardware.
- Writing ChatGPT/Codex internal state files.

## Documentation references

- [ChatGPT Voice](https://learn.chatgpt.com/docs/features/voice) documents Voice
  in Codex tasks, its configurable Voice Chat hotkey, natural interruption, and
  the single-active-Voice-Chat limit.
- [Codex Micro](https://learn.chatgpt.com/docs/features/codex-micro) demonstrates
  the desired product semantics: hardware chat selection/focus plus microphone
  and Voice Chat controls. It documents support for Codex Micro/Creator Micro 2;
  it does not document a generic hardware integration protocol that this board
  can assume.

Revalidate these Desktop integration details when implementation begins because
Voice availability and supported hardware behavior can change between app
releases.
