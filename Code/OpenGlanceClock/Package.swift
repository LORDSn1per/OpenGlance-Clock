// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "OpenGlanceClock",
    platforms: [.macOS(.v14)],
    products: [.executable(name: "OpenGlanceClock", targets: ["OpenGlanceClock"])],
    targets: [
        .target(name: "GlanceProtocol"),
        .executableTarget(name: "OpenGlanceClock", dependencies: ["GlanceProtocol"], resources: [.process("Resources")]),
        .executableTarget(name: "ProtocolChecks", dependencies: ["GlanceProtocol"], path: "Tests/GlanceProtocolTests")
    ]
)
