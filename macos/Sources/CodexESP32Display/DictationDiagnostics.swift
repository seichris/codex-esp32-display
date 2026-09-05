import Foundation

/// Only operational metadata: never audio, transcript text, task IDs or tokens.
enum DictationDiagnostics {
    private static let queue = DispatchQueue(label: "display.dictation.diagnostics")
    static func record(_ stage: String, samples: Int = 0, peak: Float = 0) {
        queue.async {
            let root = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/CodexESP32Display")
            let file = root.appendingPathComponent("dictation.log")
            try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
            if !FileManager.default.fileExists(atPath: file.path) {
                _ = FileManager.default.createFile(atPath: file.path, contents: nil, attributes: [.posixPermissions: 0o600])
            }
            guard let handle = try? FileHandle(forWritingTo: file) else { return }
            defer { try? handle.close() }
            let text = "\(ISO8601DateFormatter().string(from: Date())) stage=\(stage) samples=\(samples) peak=\(peak)\n"
            do { try handle.seekToEnd(); try handle.write(contentsOf: Data(text.utf8)) } catch { }
        }
    }
}
