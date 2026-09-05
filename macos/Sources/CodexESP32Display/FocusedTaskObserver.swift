import AppKit
import ApplicationServices
import Combine

/// Read-only observer. No shortcuts, app activation, clipboard access or title
/// matching. A missing exact document URL produces an unavailable selection.
@MainActor
final class FocusedTaskObserver: ObservableObject {
    @Published private var lastSelection = FocusedTaskSelection.unavailable()
    private let queue = DispatchQueue(label: "display.focus-observer", qos: .utility)
    private var timer: Timer?
    private var workspaceObservers: [NSObjectProtocol] = []
    private var reading = false
    private var lastPID: pid_t?

    var selection: FocusedTaskSelection { lastSelection.fresh() }

    init() {
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in self?.refresh() }
        }
        for name in [NSWorkspace.didActivateApplicationNotification, NSWorkspace.didTerminateApplicationNotification] {
            workspaceObservers.append(NSWorkspace.shared.notificationCenter.addObserver(
                forName: name, object: nil, queue: .main
            ) { [weak self] _ in
                Task { @MainActor [weak self] in self?.refresh() }
            })
        }
        refresh()
    }

    deinit {
        timer?.invalidate()
        for observer in workspaceObservers { NSWorkspace.shared.notificationCenter.removeObserver(observer) }
    }

    private func refresh() {
        let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "com.openai.codex")
        let app = apps.first(where: { $0.isActive })
            ?? apps.first(where: { $0.processIdentifier == lastPID })
            ?? (apps.count == 1 ? apps[0] : nil)
        guard let app, AXIsProcessTrusted() else {
            lastSelection = .unavailable()
            return
        }
        let pid = app.processIdentifier
        if lastPID != pid { lastSelection = .unavailable() }
        lastPID = pid
        guard !reading else { return }
        reading = true
        queue.async { [weak self] in
            let result = Self.readDocument(pid: pid)
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.reading = false
                guard self.lastPID == pid, AXIsProcessTrusted(),
                      NSRunningApplication(processIdentifier: pid)?.isTerminated == false else {
                    self.lastSelection = .unavailable()
                    return
                }
                self.lastSelection = result
            }
        }
    }

    nonisolated private static func readDocument(pid: pid_t) -> FocusedTaskSelection {
        let app = AXUIElementCreateApplication(pid)
        AXUIElementSetMessagingTimeout(app, 0.1)
        func value(_ node: AXUIElement, _ attribute: String) -> CFTypeRef? {
            var result: CFTypeRef?
            guard AXUIElementCopyAttributeValue(node, attribute as CFString, &result) == .success else { return nil }
            return result
        }
        func element(_ raw: CFTypeRef?) -> AXUIElement? {
            guard let raw, CFGetTypeID(raw) == AXUIElementGetTypeID() else { return nil }
            return (raw as! AXUIElement)
        }
        func urlText(_ raw: CFTypeRef?) -> String? {
            if let url = raw as? URL { return url.absoluteString }
            return raw as? String
        }
        guard let window = element(value(app, kAXFocusedWindowAttribute))
            ?? element(value(app, kAXMainWindowAttribute)),
              (value(window, kAXMinimizedAttribute) as? Bool) != true else { return .unavailable() }
        if let document = urlText(value(window, kAXDocumentAttribute)),
           document.hasPrefix("app://-/") {
            return .document(document)
        }

        // Inspect only shallow structural containers until reaching a root
        // web area. Never descend into web content or embedded browser panels.
        let deadline = Date().addingTimeInterval(0.4)
        var pending: [(AXUIElement, Int)] = [(window, 0)]
        var candidates: [String] = []
        var visited = 0
        while !pending.isEmpty, visited < 32, Date() < deadline {
            let (node, depth) = pending.removeFirst()
            visited += 1
            if (value(node, kAXRoleAttribute) as? String) == "AXWebArea" {
                if let url = urlText(value(node, kAXURLAttribute)), url.hasPrefix("app://-/") { candidates.append(url) }
                continue
            }
            guard let children = value(node, kAXChildrenAttribute) as? [AXUIElement], !children.isEmpty else { continue }
            guard depth < 5, children.count <= 32 - visited - pending.count else { return .unavailable() }
            pending.append(contentsOf: children.map { ($0, depth + 1) })
        }
        // Incomplete or ambiguous traversal cannot establish the active document.
        guard pending.isEmpty, Date() < deadline, candidates.count == 1 else { return .unavailable() }
        return .document(candidates[0])
    }
}
