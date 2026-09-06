import AppKit
import SwiftUI

/// A compact, non-activating recording indicator modeled after FluidVoice's
/// bottom overlay. It deliberately has no controls: the device button remains
/// the source of truth for starting and stopping dictation.
@MainActor
final class DictationRecordingOverlayController {
    static let shared = DictationRecordingOverlayController()

    private let bottomOffset: CGFloat = 50
    private var panel: NSPanel?
    private var targetScreen: NSScreen?

    private init() {}

    func show(model: DictationModel) {
        if panel == nil {
            createPanel(model: model)
        } else if let hostingView = panel?.contentView as? NSHostingView<DictationRecordingOverlayView> {
            hostingView.rootView = DictationRecordingOverlayView(model: model)
        }

        // Keep the display selected at the start of a session. Re-rendering
        // the view for the first audio/transcript callbacks must not make the
        // overlay jump if the pointer happens to cross a display boundary.
        if targetScreen == nil || panel?.isVisible != true {
            targetScreen = screenForCurrentPointer()
        }
        positionPanel()
        panel?.alphaValue = 1
        panel?.orderFrontRegardless()
    }

    func hide() {
        panel?.alphaValue = 0
        panel?.orderOut(nil)
        targetScreen = nil
    }

    private func createPanel(model: DictationModel) {
        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 300, height: 104),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        panel.isFloatingPanel = true
        panel.level = .floating
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.isMovableByWindowBackground = false
        panel.hidesOnDeactivate = false
        panel.animationBehavior = .none
        panel.ignoresMouseEvents = true

        let hostingView = NSHostingView(rootView: DictationRecordingOverlayView(model: model))
        let size = NSSize(width: 300, height: 104)
        hostingView.frame = NSRect(origin: .zero, size: size)
        hostingView.wantsLayer = true
        hostingView.layer?.backgroundColor = NSColor.clear.cgColor
        panel.setContentSize(size)
        panel.contentView = hostingView
        self.panel = panel
    }

    private func positionPanel() {
        guard let panel else { return }
        guard let screen = targetScreen ?? screenForCurrentPointer() ?? NSScreen.main ?? NSScreen.screens.first else {
            return
        }

        let visibleFrame = screen.visibleFrame
        let size = panel.frame.size
        // Match FluidVoice: center against the physical display, then offset
        // upward from the usable bottom edge so the panel clears the Dock.
        let x = screen.frame.midX - size.width / 2
        let preferredY = visibleFrame.minY + bottomOffset
        let minY = visibleFrame.minY + 10
        let maxY = max(minY, visibleFrame.maxY - size.height - 20)
        let y = min(max(preferredY, minY), maxY)
        panel.setFrameOrigin(NSPoint(x: x, y: y))
    }

    private func screenForCurrentPointer() -> NSScreen? {
        let point = NSEvent.mouseLocation
        return NSScreen.screens.first(where: { $0.frame.contains(point) })
    }
}

private struct DictationRecordingOverlayView: View {
    @ObservedObject var model: DictationModel
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    private var phase: DictationSession.Phase { model.session.phase }

    private var phaseLabel: String {
        switch phase {
        case .starting: return "Starting"
        case .recording: return "Listening"
        case .transcribing: return "Transcribing"
        case .idle, .draft, .error: return "Dictation"
        }
    }

    private var previewText: String {
        let text = model.draftText
            .replacingOccurrences(of: "\n", with: " ")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if !text.isEmpty {
            let limit = 180
            return text.count > limit ? "…" + text.suffix(limit) : text
        }
        return phase == .transcribing ? "Finishing transcription…" : "Speak now…"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack(spacing: 9) {
                Image(systemName: phase == .transcribing ? "waveform" : "mic.fill")
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundStyle(Color.cyan)

                DictationWaveformView(
                    level: model.level,
                    isRecording: phase == .recording,
                    reduceMotion: reduceMotion
                )
                .frame(width: 90, height: 20)

                Text(phaseLabel)
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(Color.cyan)
                    .lineLimit(1)
            }

            Text(previewText)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(.white.opacity(0.9))
                .lineLimit(2)
                .truncationMode(.head)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
        .frame(width: 300, height: 104, alignment: .topLeading)
        .background {
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(Color.black.opacity(0.94))
                .overlay {
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .strokeBorder(
                            LinearGradient(
                                colors: [.white.opacity(0.28), .white.opacity(0.08)],
                                startPoint: .top,
                                endPoint: .bottom
                            ),
                            lineWidth: 1
                        )
                }
        }
        .shadow(color: .black.opacity(0.4), radius: 12, y: 4)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Device Dictation — \(phaseLabel). \(previewText)")
    }
}

private struct DictationWaveformView: View {
    let level: Float
    let isRecording: Bool
    let reduceMotion: Bool
    @State private var barHeights = Array(repeating: CGFloat(5), count: 7)

    private let profile: [CGFloat] = [0.56, 0.82, 1.0, 0.72, 0.95, 0.68, 0.46]
    private let minHeight: CGFloat = 5
    private let maxHeight: CGFloat = 16

    var body: some View {
        HStack(alignment: .center, spacing: 3.5) {
            ForEach(barHeights.indices, id: \.self) { index in
                RoundedRectangle(cornerRadius: 1.5, style: .continuous)
                    .fill(Color.cyan.opacity(0.92))
                    .frame(width: 3, height: barHeights[index])
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .onAppear { updateHeights() }
        .onChange(of: level) { _ in updateHeights() }
        .onChange(of: isRecording) { _ in updateHeights() }
    }

    private func updateHeights() {
        let amplitude = isRecording
            ? CGFloat(max(0, min(level, 1)))
            : 0
        let envelope = CGFloat(pow(Double(amplitude), 0.55))
        let heights = profile.map { minHeight + (maxHeight - minHeight) * envelope * $0 }

        if reduceMotion {
            barHeights = heights
        } else {
            withAnimation(.easeOut(duration: 0.08)) {
                barHeights = heights
            }
        }
    }
}
