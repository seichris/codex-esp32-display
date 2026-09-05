import AppKit
import SwiftUI

struct FocusedTaskStatusView: View {
    @ObservedObject var observer: FocusedTaskObserver

    var body: some View {
        Section("Current task on Mac") {
            Label(observer.statusMessage, systemImage: observer.selection.status == .confirmed
                ? "checkmark.circle.fill" : "questionmark.circle")
            Text("The device follows your open Codex task. If multiple task views are open, selection stays unavailable.")
                .font(.caption).foregroundStyle(.secondary)
            Button("Show Detection Log") {
                NSWorkspace.shared.activateFileViewerSelecting([FocusedTaskDiagnostics.fileURL])
            }
        }
    }

}
