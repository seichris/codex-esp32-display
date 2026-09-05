import Foundation

/// Session ownership is independent of the currently selected task.
struct DictationSession {
    enum Phase: String { case idle, starting, recording, transcribing, draft, error }
    private(set) var phase: Phase = .idle
    private(set) var id: UUID?
    private(set) var threadId: String?
    private(set) var text = ""
    private(set) var error: String?
    var isBusy: Bool { [.starting, .recording, .transcribing].contains(phase) }

    mutating func begin(threadId: String) throws -> UUID {
        guard !isBusy, text.isEmpty else { throw DictationError.message("Finish or discard the existing dictation first.") }
        let id = UUID()
        self.id = id
        self.threadId = threadId
        text = ""
        error = nil
        phase = .starting
        return id
    }

    mutating func recording(_ id: UUID) {
        guard self.id == id, phase == .starting else { return }
        phase = .recording
    }

    mutating func finishing(_ id: UUID) {
        guard self.id == id, phase == .recording || phase == .starting else { return }
        phase = .transcribing
    }

    mutating func update(_ id: UUID, text: String, final: Bool) {
        guard self.id == id, isBusy else { return }
        self.text = text
        if final {
            if text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                fail(id, "No speech was recognized. Check the microphone level and try again.")
            } else { phase = .draft }
        }
    }

    mutating func fail(_ id: UUID, _ message: String) {
        guard self.id == id, isBusy else { return }
        error = message
        phase = .error
    }

    mutating func discard() { self = DictationSession() }
}

enum DictationError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case let .message(text) = self { return text }; return nil }
}

struct DictationDraftLink {
    static func url(threadId: String, text: String) -> URL? {
        guard UUID(uuidString: threadId) != nil, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              text.utf8.count <= 24_000 else { return nil }
        var components = URLComponents()
        components.scheme = "codex"
        components.host = "threads"
        components.path = "/" + threadId
        components.queryItems = [URLQueryItem(name: "prompt", value: text)]
        return components.url
    }
}
