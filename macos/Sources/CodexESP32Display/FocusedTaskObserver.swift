import AppKit
import ApplicationServices
import Combine

/// Enables Electron's documented accessibility support, then reads only window
/// metadata. No shortcuts, app activation, clipboard access or title matching.
@MainActor
final class FocusedTaskObserver: ObservableObject {
    @Published private var lastSelection = FocusedTaskSelection.unavailable()
    private let queue = DispatchQueue(label: "display.focus-observer", qos: .utility)
    private var timer: Timer?
    private var workspaceObservers: [NSObjectProtocol] = []
    private var reading = false
    private var lastPID: pid_t?
    private var preparedPID: pid_t?
    @Published private(set) var diagnostic = FocusedTaskDiagnostic(reason: "app-not-running")

    var statusMessage: String {
        if !AXIsProcessTrusted() { return "Accessibility permission needed" }
        if lastSelection.status == .confirmed && selection.status == .unavailable {
            return FocusedTaskDiagnostic(reason: "stale").message
        }
        return diagnostic.message
    }

    private func publish(_ selection: FocusedTaskSelection, _ diagnostic: FocusedTaskDiagnostic) {
        lastSelection = selection
        self.diagnostic = diagnostic
        FocusedTaskDiagnostics.record(diagnostic)
    }

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
        let trusted = AXIsProcessTrusted()
        guard let app, trusted else {
            preparedPID = nil
            lastPID = nil
            var diagnostic = FocusedTaskDiagnostic(reason: !trusted ? "permission-needed" : (apps.isEmpty ? "app-not-running" : "ambiguous-app"))
            diagnostic.trusted = trusted
            diagnostic.appCount = apps.count
            publish(.unavailable(), diagnostic)
            return
        }
        let pid = app.processIdentifier
        if lastPID != pid { lastSelection = .unavailable() }
        lastPID = pid
        guard !reading else { return }
        reading = true
        let prepareAccessibility = preparedPID != pid
        preparedPID = pid
        let appCount = apps.count
        queue.async { [weak self] in
            let reading = Self.readDocument(pid: pid, prepareAccessibility: prepareAccessibility)
            var diagnostic = reading.1
            diagnostic.appCount = appCount
            let result = (reading.0, diagnostic)
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.reading = false
                guard self.lastPID == pid, AXIsProcessTrusted(),
                      NSRunningApplication(processIdentifier: pid)?.isTerminated == false else {
                    self.lastSelection = .unavailable()
                    return
                }
                self.publish(result.0, result.1)
            }
        }
    }

    nonisolated private static func readDocument(pid: pid_t, prepareAccessibility: Bool) -> (FocusedTaskSelection, FocusedTaskDiagnostic) {
        let started = Date()
        let deadline = started.addingTimeInterval(1.2)
        var diagnostic = FocusedTaskDiagnostic(reason: "document-unavailable")
        diagnostic.processID = pid
        diagnostic.trusted = true
        let app = AXUIElementCreateApplication(pid)
        AXUIElementSetMessagingTimeout(app, 0.15)
        if prepareAccessibility {
            // Electron explicitly documents this for third-party assistive apps.
            // Accessibility trust alone does not cause Chromium to build its tree.
            diagnostic.accessibilitySetup = AXUIElementSetAttributeValue(
                app, "AXManualAccessibility" as CFString, kCFBooleanTrue
            ).rawValue
        }
        func finish(_ reason: String, _ selection: FocusedTaskSelection = .unavailable()) -> (FocusedTaskSelection, FocusedTaskDiagnostic) {
            diagnostic.reason = reason
            diagnostic.elapsedMilliseconds = Int(Date().timeIntervalSince(started) * 1000)
            return (selection, diagnostic)
        }
        func documentResult(_ document: String) -> (FocusedTaskSelection, FocusedTaskDiagnostic) {
            let selection = FocusedTaskSelection.document(document)
            let reason = URLComponents(string: document)?.path == "/index.html"
                ? "shell-document" : selection.status.rawValue
            return finish(reason, selection)
        }
        func value(_ node: AXUIElement, _ attribute: String) -> CFTypeRef? {
            guard Date() < deadline else { return nil }
            var result: CFTypeRef?
            let error = AXUIElementCopyAttributeValue(node, attribute as CFString, &result)
            guard error == .success else {
                diagnostic.axErrors[attribute] = error.rawValue
                return nil
            }
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
            ?? element(value(app, kAXMainWindowAttribute)) else {
            return finish(Date() >= deadline ? "time-limit" : "window-unavailable")
        }
        guard (value(window, kAXMinimizedAttribute) as? Bool) != true else { return finish("window-minimized") }
        if let document = urlText(value(window, kAXDocumentAttribute)), document.hasPrefix("app://-/") {
            return documentResult(document)
        }

        // Scan structural containers only. Root web areas are boundaries: never
        // inspect transcript descendants or links inside embedded browser content.
        var pending: [(AXUIElement, Int)] = [(window, 0)]
        var candidates: [String] = []
        let maximumNodes = 96
        while !pending.isEmpty, diagnostic.visited < maximumNodes, Date() < deadline {
            let (node, depth) = pending.removeFirst()
            diagnostic.visited += 1
            guard let role = value(node, kAXRoleAttribute) as? String else { return finish("read-failed") }
            if role == "AXWebArea" {
                diagnostic.webAreas += 1
                if let url = urlText(value(node, kAXURLAttribute)), url.hasPrefix("app://-/") {
                    candidates.append(url)
                    diagnostic.candidates = candidates.count
                }
                continue
            }
            // Bound the read itself instead of fetching an arbitrarily large
            // AXChildren array and checking its count after allocation.
            var count: CFIndex = 0
            let countError = AXUIElementGetAttributeValueCount(node, kAXChildrenAttribute as CFString, &count)
            if countError == .attributeUnsupported || countError == .noValue { continue }
            guard countError == .success else {
                diagnostic.axErrors["AXChildrenCount"] = countError.rawValue
                return finish("read-failed")
            }
            guard count > 0 else { continue }
            guard depth < 8, count <= maximumNodes - diagnostic.visited - pending.count else { return finish("tree-limit") }
            guard Date() < deadline else { return finish("time-limit") }
            var rawChildren: CFArray?
            let childrenError = AXUIElementCopyAttributeValues(node, kAXChildrenAttribute as CFString, 0, count, &rawChildren)
            guard childrenError == .success, let children = rawChildren as? [AXUIElement] else {
                diagnostic.axErrors["AXChildren"] = childrenError.rawValue
                return finish("read-failed")
            }
            pending.append(contentsOf: children.map { ($0, depth + 1) })
        }
        guard Date() < deadline else { return finish("time-limit") }
        guard pending.isEmpty else { return finish("tree-limit") }
        guard candidates.count == 1 else { return finish(candidates.isEmpty ? "document-unavailable" : "multiple-documents") }
        return documentResult(candidates[0])
    }
}
