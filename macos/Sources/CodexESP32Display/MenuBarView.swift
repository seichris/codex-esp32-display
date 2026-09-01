import AppKit
import SwiftUI

struct MenuBarView: View {
    @ObservedObject var bridge: BridgeController

    var body: some View {
        Section {
            Label(bridge.state.title, systemImage: bridge.state.symbolName)
            Label(bridge.health.title, systemImage: healthSymbol)
        }

        Divider()

        Button {
            bridge.isRunning ? bridge.stop() : bridge.start()
        } label: {
            Label(
                bridge.isRunning ? "Stop Bridge" : "Start Bridge",
                systemImage: bridge.isRunning ? "stop.fill" : "play.fill"
            )
        }

        Button {
            bridge.openDashboard()
        } label: {
            Label("Open Dashboard", systemImage: "safari")
        }
        .disabled(!bridge.isRunning)

        Button {
            bridge.copyEndpoint()
        } label: {
            Label("Copy Device Endpoint", systemImage: "doc.on.doc")
        }

        Button {
            bridge.revealLogs()
        } label: {
            Label("Reveal Logs", systemImage: "folder")
        }

        Divider()

        Button {
            NSApplication.shared.terminate(nil)
        } label: {
            Label("Quit", systemImage: "power")
        }
    }

    private var healthSymbol: String {
        switch bridge.health {
        case .healthy(let connected):
            return connected ? "checkmark.circle" : "arrow.triangle.2.circlepath"
        case .unknown:
            return "questionmark.circle"
        case .unavailable:
            return "xmark.circle"
        }
    }
}
