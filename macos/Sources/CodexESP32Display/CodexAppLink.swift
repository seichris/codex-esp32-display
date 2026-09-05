import AppKit

@MainActor
enum CodexAppLink {
    static var applicationURL: URL? {
        let running = NSRunningApplication.runningApplications(withBundleIdentifier: "com.openai.codex")
        if running.count == 1 { return running[0].bundleURL }
        if running.count > 1 { return running.first(where: { $0.isActive })?.bundleURL }
        return NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.openai.codex")
    }

    static func open(_ url: URL) async throws {
        guard let app = applicationURL else {
            throw DictationError.message("Open the intended Codex app first; its application could not be identified uniquely.")
        }
        try await withCheckedThrowingContinuation { (completion: CheckedContinuation<Void, Error>) in
            NSWorkspace.shared.open([url], withApplicationAt: app, configuration: NSWorkspace.OpenConfiguration()) { _, error in
                if let error { completion.resume(throwing: error) }
                else { completion.resume() }
            }
        }
    }
}
