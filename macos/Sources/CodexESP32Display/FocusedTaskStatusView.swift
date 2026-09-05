import AppKit
import ApplicationServices
import SwiftUI

struct FocusedTaskStatusView: View {
    @ObservedObject var observer: FocusedTaskObserver

    var body: some View {
        Section("Current task on Mac") {
            Label(observer.statusMessage, systemImage: observer.selection.status == .confirmed
                ? "checkmark.circle.fill" : "questionmark.circle")
            Text("The device follows the task in your focused Codex window when its identity can be read.")
                .font(.caption).foregroundStyle(.secondary)
            Button("Show Detection Log") {
                NSWorkspace.shared.activateFileViewerSelecting([FocusedTaskDiagnostics.fileURL])
            }
            if !AXIsProcessTrusted() {
                Button("Allow Task Detection") {
                    let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
                    _ = AXIsProcessTrustedWithOptions(options)
                }
            }
        }
    }

}
