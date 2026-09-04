import AppKit
import SwiftUI

@MainActor
final class CodexESP32DisplayAppDelegate: NSObject, NSApplicationDelegate {
    private let bridge = BridgeController()
    private var statusItemController: StatusItemController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        statusItemController = StatusItemController(bridge: bridge)
    }

    func applicationWillTerminate(_ notification: Notification) {
        statusItemController?.invalidate()
        BridgeController.active?.stop()
    }
}

@main
struct CodexESP32DisplayApp: App {
    @NSApplicationDelegateAdaptor(CodexESP32DisplayAppDelegate.self) private var appDelegate

    var body: some Scene {
        Settings {
            VoiceSettingsView()
        }
    }
}
