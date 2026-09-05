import CoreGraphics
import XCTest
@testable import CodexESP32Display

final class VoiceShortcutTests: XCTestCase {
    func testParsesNamedShortcut() {
        let shortcut = VoiceShortcut.parse("control+option+space")
        XCTAssertEqual(shortcut?.keyCode, 49)
        XCTAssertTrue(shortcut?.flags.contains(.maskControl) == true)
        XCTAssertTrue(shortcut?.flags.contains(.maskAlternate) == true)
    }

    func testRejectsMissingModifierAndUnknownKey() {
        XCTAssertNil(VoiceShortcut.parse("space"))
        XCTAssertNil(VoiceShortcut.parse("control+banana"))
    }
}
