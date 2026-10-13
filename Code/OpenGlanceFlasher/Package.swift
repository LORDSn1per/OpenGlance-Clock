// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "OpenGlanceFlasher",
    platforms: [.macOS(.v14)],
    products: [.executable(name: "OpenGlanceFlasher", targets: ["OpenGlanceFlasher"])],
    targets: [
        .target(name: "FlasherCore"),
        .executableTarget(name: "OpenGlanceFlasher", dependencies: ["FlasherCore"]),
        .executableTarget(name: "FlasherChecks", dependencies: ["FlasherCore"], path: "Tests/FlasherChecks")
    ]
)
