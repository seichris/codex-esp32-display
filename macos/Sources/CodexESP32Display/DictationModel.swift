import AppKit
import AVFoundation
import Combine
import Speech
import SwiftUI

@MainActor
final class DictationModel: ObservableObject {
    @Published private(set) var session = DictationSession()
    @Published var draftText = ""
    @Published private(set) var level: Float = 0
    @Published private(set) var handoffMessage: String?
    @Published private(set) var permissionRevision = 0
    var onStateChange: (() -> Void)?
    private let recorder = DictationRecorder()
    private var window: NSWindow?

    var ready: Bool { DictationRecorder.permissionReady && DictationRecorder.device != nil && DictationRecorder.onDeviceAvailable }
    var wireState: String {
        switch session.phase {
        case .idle, .draft: return "ready"
        case .starting: return "starting"
        case .recording: return "listening"
        case .transcribing: return "muted" // Existing firmware closes its PCM gate.
        case .error: return "error"
        }
    }
    var status: String {
        switch session.phase {
        case .idle: return ready ? "Dictation ready" : "Dictation needs setup"
        case .starting: return "Starting Waveshare microphone"
        case .recording: return "Recording from Waveshare"
        case .transcribing: return "Finishing transcription"
        case .draft: return "Dictation draft ready"
        case .error: return session.error ?? "Dictation failed"
        }
    }

    func requestPermissions() async {
        _ = await AVCaptureDevice.requestAccess(for: .audio)
        _ = await withCheckedContinuation { completion in
            SFSpeechRecognizer.requestAuthorization { completion.resume(returning: $0) }
        }
        permissionRevision += 1
        onStateChange?()
    }

    func start(threadId: String) async throws {
        guard UUID(uuidString: threadId) != nil else { throw DictationError.message("Dictation requires a local Codex task ID.") }
        guard ready else { throw DictationError.message("Open Voice Settings and enable Microphone and Speech Recognition, then connect the board by USB.") }
        let id = try session.begin(threadId: threadId)
        handoffMessage = nil
        draftText = ""
        showWindow()
        onStateChange?()
        do {
            try await recorder.start(id: id) { [weak self] event in
                Task { @MainActor in self?.handle(id: id, event: event) }
            }
            session.recording(id)
            onStateChange?()
        } catch {
            session.fail(id, error.localizedDescription)
            onStateChange?()
            throw DictationError.message(session.error ?? error.localizedDescription)
        }
    }

    func finish(threadId: String) throws {
        guard session.threadId == threadId else { throw DictationError.message("This recording belongs to a different task.") }
        if let id = session.id, session.isBusy {
            session.finishing(id)
            recorder.finish()
            onStateChange?()
        }
    }

    private func handle(id: UUID, event: DictationRecorder.Event) {
        guard session.id == id else { return }
        let previousPhase = session.phase
        switch event {
        case .recording: session.recording(id)
        case .finishing: session.finishing(id)
        case let .transcript(text, final):
            session.update(id, text: text, final: final)
            draftText = session.text
            if final { level = 0; showWindow() }
        case let .failed(message): session.fail(id, message); level = 0; showWindow()
        case let .level(value): level = value
        }
        if previousPhase != session.phase { DictationDiagnostics.record(session.phase.rawValue) }
        onStateChange?()
    }

    func discard() {
        guard !session.isBusy else { return }
        recorder.cancel()
        session.discard()
        draftText = ""
        handoffMessage = nil
        level = 0
        onStateChange?()
    }

    func openDraft() {
        guard !session.isBusy, let threadId = session.threadId,
              let url = DictationDraftLink.url(threadId: threadId, text: draftText) else {
            handoffMessage = "The draft is empty or too long. Use Copy Text to keep it."
            return
        }
        handoffMessage = "Opening the recorded task's draft…"
        Task {
            do {
                try await CodexAppLink.open(url)
                handoffMessage = "Draft link opened for the recorded task. Check the text in Codex before sending. This copy is kept until you discard it."
                DictationDiagnostics.record("draft-link-accepted")
            } catch {
                handoffMessage = "Codex could not open the draft. Your text is still here."
                DictationDiagnostics.record("draft-link-failed")
            }
        }
    }

    func copyText() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(draftText, forType: .string)
        handoffMessage = "Text copied."
    }

    func showWindow() {
        if window == nil {
            let window = NSWindow(contentViewController: NSHostingController(rootView: DictationReviewView(model: self)))
            window.title = "Device Dictation"
            window.styleMask = [.titled, .closable, .miniaturizable, .resizable]
            window.isReleasedWhenClosed = false
            window.center()
            self.window = window
        }
        NSApplication.shared.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
    }
}

private struct DictationReviewView: View {
    @ObservedObject var model: DictationModel
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(model.status).font(.headline)
            if let id = model.session.threadId {
                Text("Task: \(id)").font(.caption).textSelection(.enabled)
            }
            if model.session.isBusy {
                ProgressView(value: Double(model.level)).accessibilityLabel("Waveshare microphone input level")
                Text("Long press the device button again to finish. Recording stops after 55 seconds.").font(.caption)
            }
            TextEditor(text: $model.draftText).font(.body)
                .disabled(model.session.isBusy).accessibilityLabel("Dictation text")
            if let error = model.session.error { Text(error).foregroundStyle(.red) }
            if let message = model.handoffMessage { Text(message).font(.caption) }
            Text("Open as Task Draft replaces any existing text in that task's composer. It does not send a message.")
                .font(.caption).foregroundStyle(.secondary)
            HStack {
                Button("Discard") { model.discard() }.disabled(model.session.isBusy)
                Spacer()
                Button("Copy Text") { model.copyText() }.disabled(model.draftText.isEmpty)
                Button("Open as Task Draft") { model.openDraft() }
                    .disabled(model.session.isBusy || model.draftText.isEmpty)
            }
        }
        .padding(20).frame(minWidth: 560, minHeight: 390)
    }
}
