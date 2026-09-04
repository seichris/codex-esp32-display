import AppKit
import ApplicationServices
import SwiftUI

struct VoiceSettingsView: View {
    @AppStorage("VoiceShortcut") private var shortcut = "control+option+space"
    @AppStorage("VoiceDeepLinkTemplate") private var deepLink = "codex://threads/{threadId}"

    var body: some View {
        Form {
            Section("Desktop Voice") {
                TextField("Voice hotkey", text: $shortcut)
                Text("Use the same shortcut in Codex Settings > Voice. Example: control+option+space")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                TextField("Task deep link", text: $deepLink)
                Text("The template must contain {threadId}. Task focus remains inferred until Codex exposes an acknowledgement.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("Readiness") {
                readiness("Codex Desktop installed", ok: DesktopVoiceController.codexURL != nil)
                readiness("Voice shortcut valid", ok: VoiceShortcut.parse(shortcut) != nil)
                readiness("Task deep link valid", ok: DesktopVoiceController.deepLinkTemplateIsValid)
                readiness("Accessibility permission", ok: AXIsProcessTrusted())
                Button("Request Accessibility Permission") {
                    let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
                    _ = AXIsProcessTrustedWithOptions(options)
                }
            }
        }
        .formStyle(.grouped)
        .padding()
        .frame(width: 520, height: 390)
    }

    private func readiness(_ title: String, ok: Bool) -> some View {
        Label(title, systemImage: ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
            .foregroundStyle(ok ? .green : .orange)
    }
}
