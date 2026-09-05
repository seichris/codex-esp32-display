import XCTest
@testable import CodexESP32Display

final class DictationModelTests: XCTestCase {
    @MainActor
    func testCompletedDictationAutomaticallyOpensOriginalTaskOnceAndKeepsCopy() async throws {
        let target = "01a06f9d-249c-7f43-b019-95324c366b8c"
        var session = DictationSession()
        let id = try session.begin(threadId: target)
        session.recording(id)
        var opened: [URL] = []
        let didOpen = expectation(description: "Opened draft link")
        let model = DictationModel(session: session) { url in
            opened.append(url)
            didOpen.fulfill()
        }

        model.handle(id: id, event: .transcript("Keep these words", final: false))
        XCTAssertTrue(opened.isEmpty)
        model.handle(id: id, event: .finishing)
        model.handle(id: id, event: .transcript("", final: true))
        await fulfillment(of: [didOpen], timeout: 2)
        model.handle(id: id, event: .transcript("Late revision", final: true))

        XCTAssertEqual(opened, [try XCTUnwrap(DictationDraftLink.url(threadId: target, text: "Keep these words"))])
        XCTAssertEqual(model.draftText, "Keep these words")
        XCTAssertEqual(model.session.phase, .draft)
    }

    @MainActor
    func testOldRecordingCannotOpenComposerForNewRecording() throws {
        var session = DictationSession()
        let old = try session.begin(threadId: "01a06f9d-249c-7f43-b019-95324c366b8c")
        session.discard()
        _ = try session.begin(threadId: "01a070ea-33cd-7642-89f9-86181a0c85ad")
        let model = DictationModel(session: session) { _ in XCTFail("Must not open a stale recording") }
        model.handle(id: old, event: .transcript("Old words", final: true))
        XCTAssertTrue(model.draftText.isEmpty)
        XCTAssertFalse(model.isOpeningDraft)
    }
}
