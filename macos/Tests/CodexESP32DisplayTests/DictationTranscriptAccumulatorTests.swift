import XCTest
@testable import CodexESP32Display

final class DictationTranscriptAccumulatorTests: XCTestCase {
    func testCompletedPartialKeepsEarlierUtterancesAfterPause() {
        var accumulator = DictationTranscriptAccumulator()

        XCTAssertEqual(accumulator.update("First phrase", completedPartial: false), "First phrase")
        XCTAssertEqual(accumulator.update("First phrase", completedPartial: true), "First phrase")
        XCTAssertEqual(accumulator.update("second phrase", completedPartial: false), "First phrase second phrase")
        XCTAssertEqual(accumulator.update("second phrase", completedPartial: true), "First phrase second phrase")
    }

    func testBoundaryAfterResetPreservesTheActiveEarlierUtterance() {
        var accumulator = DictationTranscriptAccumulator()

        XCTAssertEqual(accumulator.update("First phrase", completedPartial: false), "First phrase")
        XCTAssertEqual(accumulator.update("second phrase", completedPartial: true), "First phrase second phrase")
    }

    func testFinalResultAfterUnmarkedResetKeepsBothUtterances() {
        var accumulator = DictationTranscriptAccumulator()

        XCTAssertEqual(accumulator.update("First phrase", completedPartial: false), "First phrase")
        XCTAssertEqual(accumulator.update("second phrase", completedPartial: false), "First phrase second phrase")
        XCTAssertEqual(accumulator.update("second phrase", completedPartial: true), "First phrase second phrase")
    }

    func testCumulativeHypothesisCanGrowBeforeItIsCommitted() {
        var accumulator = DictationTranscriptAccumulator()

        XCTAssertEqual(accumulator.update("Turn", completedPartial: false), "Turn")
        XCTAssertEqual(accumulator.update("Turn left", completedPartial: false), "Turn left")
        XCTAssertEqual(accumulator.update("Turn left at the bridge", completedPartial: true), "Turn left at the bridge")
        XCTAssertEqual(accumulator.text, "Turn left at the bridge")
    }

    func testCumulativeCompletedPartialsDoNotDuplicateText() {
        var accumulator = DictationTranscriptAccumulator()

        XCTAssertEqual(accumulator.update("First", completedPartial: true), "First")
        XCTAssertEqual(accumulator.update("First second", completedPartial: true), "First second")
        XCTAssertEqual(accumulator.update("First second", completedPartial: true), "First second")
    }

    func testDisjointResultsStillAccumulateWithoutBoundaryMarker() {
        var accumulator = DictationTranscriptAccumulator()

        XCTAssertEqual(accumulator.update("First", completedPartial: false), "First")
        XCTAssertEqual(accumulator.update("second", completedPartial: false), "First second")
        XCTAssertEqual(accumulator.update("second sentence", completedPartial: false), "First second sentence")
    }

    func testEmptyResultsPreserveAccumulatedText() {
        var accumulator = DictationTranscriptAccumulator()

        _ = accumulator.update("Keep this", completedPartial: false)
        XCTAssertEqual(accumulator.update(" \n", completedPartial: true), "Keep this")
    }
}
