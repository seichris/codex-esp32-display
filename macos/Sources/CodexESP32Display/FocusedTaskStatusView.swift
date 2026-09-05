import ApplicationServices
import SwiftUI

struct FocusedTaskStatusView: View {
    @ObservedObject var observer: FocusedTaskObserver

    var body: some View {
        Section("Current task on Mac") {
            Label(status, systemImage: observer.selection.status == .confirmed
                ? "checkmark.circle.fill" : "questionmark.circle")
            Text("The device follows the task in your focused Codex window when its identity can be read.")
                .font(.caption).foregroundStyle(.secondary)
            if !AXIsProcessTrusted() {
                Button("Allow Task Detection") {
                    let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
                    _ = AXIsProcessTrustedWithOptions(options)
                }
            }
        }
    }

    private var status: String {
        guard AXIsProcessTrusted() else { return "Accessibility permission needed" }
        switch observer.selection.status {
        case .confirmed: return "Current task detected"
        case .noTask: return "No supported task open in this window"
        case .unsupportedHost: return "Remote task detection is not supported yet"
        case .unavailable: return "Current task could not be detected"
        }
    }
}
