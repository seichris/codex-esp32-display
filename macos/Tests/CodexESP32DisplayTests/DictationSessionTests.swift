import XCTest
@testable import CodexESP32Display

final class DictationSessionTests: XCTestCase {
    let first = "01a06f9d-249c-7f43-b019-95324c366b8c"
    let second = "01a06f9d-249c-7f43-b019-95324c366b8d"

    func testRecordingFinishProducesDraftForOriginalTask() throws {
        var state = DictationSession()
        let id = try state.begin(threadId: first)
        state.recording(id)
        XCTAssertEqual(state.phase, .recording)
        XCTAssertThrowsError(try state.begin(threadId: second))
        state.finishing(id)
        XCTAssertEqual(state.phase, .transcribing)
        state.update(id, text: "Turn left at the bridge.", final: true)
        XCTAssertEqual(state.phase, .draft)
        XCTAssertEqual(state.threadId, first)
        XCTAssertEqual(state.text, "Turn left at the bridge.")
        XCTAssertThrowsError(try state.begin(threadId: second))
    }

    func testDelayedCallbacksCannotChangeNextRecording() throws {
        var state = DictationSession()
        let old = try state.begin(threadId: first)
        state.fail(old, "Disconnected")
        state.discard()
        let new = try state.begin(threadId: second)
        state.update(old, text: "Wrong task text", final: true)
        state.fail(old, "Late error")
        state.recording(old)
        XCTAssertEqual(state.id, new)
        XCTAssertEqual(state.threadId, second)
        XCTAssertEqual(state.phase, .starting)
        XCTAssertTrue(state.text.isEmpty)
    }

    func testNoSpeechCannotBecomeSuccessfulDraft() throws {
        var state = DictationSession()
        let id = try state.begin(threadId: first)
        state.recording(id)
        state.finishing(id)
        state.update(id, text: " \n ", final: true)
        XCTAssertEqual(state.phase, .error)
        XCTAssertNotNil(state.error)
    }

    func testFailureRetainsPartialTextAndBlocksOverwrite() throws {
        var state = DictationSession()
        let id = try state.begin(threadId: first)
        state.recording(id)
        state.update(id, text: "Keep these words", final: false)
        state.fail(id, "Recognition timed out")
        XCTAssertEqual(state.text, "Keep these words")
        XCTAssertThrowsError(try state.begin(threadId: second))
    }

    func testStopWhileStartingCannotBeReopenedByLateReady() throws {
        var state = DictationSession()
        let id = try state.begin(threadId: first)
        state.finishing(id)
        state.recording(id)
        XCTAssertEqual(state.phase, .transcribing)
    }

    func testDraftLinkRetainsExactTargetAndLiteralText() throws {
        let text = "Fix A&B? #1 + 50%\n你好 🚲"
        let url = try XCTUnwrap(DictationDraftLink.url(threadId: first, text: text))
        let parts = try XCTUnwrap(URLComponents(url: url, resolvingAgainstBaseURL: false))
        XCTAssertEqual(parts.scheme, "codex")
        XCTAssertEqual(parts.host, "threads")
        XCTAssertEqual(parts.path, "/" + first)
        XCTAssertEqual(parts.queryItems, [URLQueryItem(name: "prompt", value: text)])
    }

    func testInvalidTargetAndEmptyOrOversizedDraftAreRejected() {
        XCTAssertNil(DictationDraftLink.url(threadId: "../../another", text: "Hello"))
        XCTAssertNil(DictationDraftLink.url(threadId: first, text: " \n"))
        XCTAssertNil(DictationDraftLink.url(threadId: first, text: String(repeating: "x", count: 24_001)))
    }
}
