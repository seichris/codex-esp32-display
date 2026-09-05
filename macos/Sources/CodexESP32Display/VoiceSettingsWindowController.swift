import AppKit
import SwiftUI

/// Own the window directly: an NSStatusItem menu does not provide SwiftUI's
/// settings environment, and responder-chain settings selectors can go unhandled.
@MainActor
final class VoiceSettingsWindowController: NSWindowController {
    init(dictation: DictationModel) {
        let window = NSWindow(contentViewController: NSHostingController(rootView: VoiceSettingsView(dictation: dictation)))
        window.title = "Voice Settings"
        window.styleMask = [.titled, .closable, .miniaturizable]
        window.isReleasedWhenClosed = false
        window.setFrameAutosaveName("VoiceSettingsWindow")
        window.center()
        super.init(window: window)
    }

    required init?(coder: NSCoder) {
        fatalError("VoiceSettingsWindowController is created programmatically")
    }

    func show() {
        showWindow(nil)
        NSApplication.shared.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
    }
}
