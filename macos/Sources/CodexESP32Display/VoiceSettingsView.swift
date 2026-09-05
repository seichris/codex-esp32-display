import AVFoundation
import Speech
import SwiftUI

struct VoiceSettingsView: View {
    @ObservedObject var dictation: DictationModel
    @AppStorage("VoiceDeepLinkTemplate") private var deepLink = "codex://threads/{threadId}"
    @State private var requesting = false
    @State private var microphoneConnected = DictationRecorder.device != nil

    var body: some View {
        Form {
            Section("Device dictation") {
                Text("Long press any button to record. Long press again to end recording and paste text into Codex.")
                Text("English transcription runs on this Mac. Audio is not saved or sent to a transcription service.")
                    .font(.caption).foregroundStyle(.secondary)
                TextField("Task deep link", text: $deepLink)
            }
            Section("Readiness") {
                readiness(microphoneConnected ? "Waveshare microphone connected" : "Waveshare microphone not detected. Connect the USB data cable.",
                          ok: microphoneConnected)
                readiness("Microphone permission", ok: AVCaptureDevice.authorizationStatus(for: .audio) == .authorized)
                readiness("Speech Recognition permission", ok: SFSpeechRecognizer.authorizationStatus() == .authorized)
                readiness("On-device English recognition", ok: DictationRecorder.onDeviceAvailable)
                Button(requesting ? "Requesting permissions…" : "Enable Dictation Permissions") {
                    requesting = true
                    Task { await dictation.requestPermissions(); requesting = false }
                }.disabled(requesting)
                Button("Open Dictation") { dictation.showWindow() }
            }
        }
        .formStyle(.grouped).padding().frame(width: 550, height: 560)
        .onAppear { microphoneConnected = DictationRecorder.device != nil }
        .onReceive(NotificationCenter.default.publisher(for: AVCaptureDevice.wasConnectedNotification)) { _ in
            microphoneConnected = DictationRecorder.device != nil
        }
        .onReceive(NotificationCenter.default.publisher(for: AVCaptureDevice.wasDisconnectedNotification)) { _ in
            microphoneConnected = DictationRecorder.device != nil
        }
    }
    private func readiness(_ title: String, ok: Bool) -> some View {
        Label(title, systemImage: ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
            .foregroundStyle(ok ? .green : .orange)
    }
}
