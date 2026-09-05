import AVFoundation
import Speech

/// All capture and recognition state is confined to queue. No audio is saved.
final class DictationRecorder: NSObject, AVCaptureAudioDataOutputSampleBufferDelegate, @unchecked Sendable {
    static let deviceID = "AppleUSBAudioEngine:Codex ESP32 Display:Waveshare Voice Microphone:CESP32VOICE01:1"
    enum Event: Sendable {
        case recording, finishing
        case transcript(String, final: Bool)
        case level(Float)
        case failed(String)
    }
    private let queue = DispatchQueue(label: "display.dictation.capture")
    private var session: AVCaptureSession?
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private var recognizer: SFSpeechRecognizer?
    private var generation: UUID?
    private var event: (@Sendable (Event) -> Void)?
    private var startCompletion: CheckedContinuation<Void, Error>?
    private var finishing = false
    private var samples = 0
    private var peak: Float = 0
    private var lastLevel = Date.distantPast
    private var startDeadline = Date.distantPast
    private var lastBufferAt = Date.distantPast

    static var device: AVCaptureDevice? { AVCaptureDevice(uniqueID: deviceID) }
    static var permissionReady: Bool {
        AVCaptureDevice.authorizationStatus(for: .audio) == .authorized
            && SFSpeechRecognizer.authorizationStatus() == .authorized
    }
    static var onDeviceAvailable: Bool {
        SFSpeechRecognizer(locale: Locale(identifier: "en-US"))?.supportsOnDeviceRecognition == true
    }

    func start(id: UUID, event: @escaping @Sendable (Event) -> Void) async throws {
        try await withCheckedThrowingContinuation { (completion: CheckedContinuation<Void, Error>) in
            queue.async {
                guard self.generation == nil else {
                    completion.resume(throwing: DictationError.message("Dictation is already active.")); return
                }
                self.generation = id
                self.event = event
                self.startCompletion = completion
                self.startDeadline = Date().addingTimeInterval(2)
                self.lastBufferAt = Date()
                self.finishing = false
                self.samples = 0
                self.peak = 0
                do {
                    guard Self.permissionReady else { throw DictationError.message("Allow Microphone and Speech Recognition in Voice Settings.") }
                    guard let device = Self.device else { throw DictationError.message("Connect the Waveshare Voice Microphone by USB.") }
                    guard let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "en-US")), recognizer.supportsOnDeviceRecognition else {
                        throw DictationError.message("On-device English speech recognition is unavailable on this Mac.")
                    }
                    let request = SFSpeechAudioBufferRecognitionRequest()
                    request.requiresOnDeviceRecognition = true
                    request.shouldReportPartialResults = true
                    request.taskHint = .dictation
                    self.request = request
                    self.recognizer = recognizer
                    self.task = recognizer.recognitionTask(with: request) { result, error in
                        self.queue.async {
                            guard self.generation == id else { return }
                            if let result {
                                self.event?(.transcript(result.bestTranscription.formattedString, final: result.isFinal))
                                if result.isFinal { self.cleanup(); return }
                            }
                            if let error { self.fail("Speech recognition failed: \(error.localizedDescription)") }
                        }
                    }
                    let capture = AVCaptureSession()
                    capture.beginConfiguration()
                    let input = try AVCaptureDeviceInput(device: device)
                    let output = AVCaptureAudioDataOutput()
                    output.audioSettings = [AVFormatIDKey: kAudioFormatLinearPCM, AVLinearPCMBitDepthKey: 16,
                        AVLinearPCMIsFloatKey: false, AVLinearPCMIsBigEndianKey: false, AVNumberOfChannelsKey: 1]
                    guard capture.canAddInput(input), capture.canAddOutput(output) else {
                        capture.commitConfiguration()
                        throw DictationError.message("Could not configure the Waveshare microphone.")
                    }
                    capture.addInput(input)
                    capture.addOutput(output)
                    output.setSampleBufferDelegate(self, queue: self.queue)
                    capture.commitConfiguration()
                    self.session = capture
                    capture.startRunning()
                    guard Date() < self.startDeadline else { throw DictationError.message("The microphone took too long to start. Try again.") }
                    self.checkBufferLiveness(id)
                    guard capture.isRunning else { throw DictationError.message("The Waveshare microphone did not start.") }
                    // A successful acknowledgement requires a real USB sample buffer.
                    self.queue.asyncAfter(deadline: .now() + 1.5) {
                        if self.generation == id, self.startCompletion != nil { self.fail("No audio buffers arrived from the Waveshare microphone.") }
                    }
                    self.queue.asyncAfter(deadline: .now() + 55) {
                        if self.generation == id { self.finishOnQueue() }
                    }
                } catch { self.fail(error.localizedDescription) }
            }
        }
    }

    func finish() { queue.async { self.finishOnQueue() } }
    func cancel() { queue.async { self.cleanup() } }

    private func finishOnQueue() {
        guard generation != nil, !finishing else { return }
        finishing = true
        DictationDiagnostics.record("finish", samples: samples, peak: peak)
        event?(.finishing)
        session?.stopRunning()
        request?.endAudio()
        let id = generation
        queue.asyncAfter(deadline: .now() + 10) {
            guard self.generation == id else { return }
            self.fail(self.samples == 0 || self.peak < 0.0001
                ? "The microphone delivered silence. Try again after the device shows LISTENING."
                : "Transcription did not finish. Any partial text is preserved for review.")
        }
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard generation != nil, !finishing else { return }
        if startCompletion != nil, Date() >= startDeadline {
            fail("The microphone took too long to deliver audio. Try again.")
            return
        }
        lastBufferAt = Date()
        samples += CMSampleBufferGetNumSamples(sampleBuffer)
        if let block = CMSampleBufferGetDataBuffer(sampleBuffer), CMBlockBufferGetDataLength(block) > 0 {
            let length = CMBlockBufferGetDataLength(block)
            var data = Data(count: length)
            let status = data.withUnsafeMutableBytes { CMBlockBufferCopyDataBytes(block, atOffset: 0, dataLength: length, destination: $0.baseAddress!) }
            if status == kCMBlockBufferNoErr {
                let level = data.withUnsafeBytes { bytes -> Float in
                    var result: Float = 0
                    for offset in stride(from: 0, to: max(0, bytes.count - 1), by: 2) {
                        let value = Int16(littleEndian: bytes.loadUnaligned(fromByteOffset: offset, as: Int16.self))
                        result = max(result, abs(Float(value)) / 32768)
                    }
                    return result
                }
                peak = max(peak, level)
                if Date().timeIntervalSince(lastLevel) > 0.15 { lastLevel = Date(); event?(.level(level)) }
            }
        }
        request?.appendAudioSampleBuffer(sampleBuffer)
        if let completion = startCompletion {
            startCompletion = nil
            DictationDiagnostics.record("recording", samples: samples, peak: peak)
            event?(.recording)
            completion.resume()
        }
    }

    private func checkBufferLiveness(_ id: UUID) {
        queue.asyncAfter(deadline: .now() + 1) {
            guard self.generation == id, !self.finishing else { return }
            if Date().timeIntervalSince(self.lastBufferAt) > 2 {
                self.fail("Audio stopped arriving from the Waveshare microphone. Check the USB connection.")
            } else { self.checkBufferLiveness(id) }
        }
    }

    private func fail(_ message: String) { DictationDiagnostics.record("capture-or-recognition-failed", samples: samples, peak: peak); event?(.failed(message)); cleanup() }

    private func cleanup() {
        generation = nil
        session?.stopRunning()
        session = nil
        request?.endAudio()
        request = nil
        task?.cancel()
        task = nil
        recognizer = nil
        event = nil
        if let completion = startCompletion {
            startCompletion = nil
            completion.resume(throwing: DictationError.message("Recording could not start. Check Voice Settings for the cause."))
        }
    }
}
