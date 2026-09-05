import Combine
import Foundation

@MainActor
final class FocusedTaskObserver: ObservableObject {
    @Published private var lastSelection = FocusedTaskSelection.unavailable()
    @Published private(set) var diagnostic = FocusedTaskDiagnostic(reason: "events-connecting")
    private let connection = TaskEventConnection()

    var selection: FocusedTaskSelection { lastSelection.fresh() }
    var statusMessage: String {
        if lastSelection.status == .confirmed && selection.status == .unavailable {
            return FocusedTaskDiagnostic(reason: "events-timeout").message
        }
        return diagnostic.message
    }
    init() {
        connection.start { [weak self] selection, diagnostic in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.lastSelection = selection
                self.diagnostic = diagnostic
                FocusedTaskDiagnostics.record(diagnostic)
            }
        }
    }
    deinit { connection.stop() }
}
