// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "CodexESP32DisplayMenuBar",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .executable(
            name: "CodexESP32Display",
            targets: ["CodexESP32Display"]
        ),
    ],
    targets: [
        .executableTarget(
            name: "CodexESP32Display",
            path: "Sources/CodexESP32Display"
        ),
    ]
)
