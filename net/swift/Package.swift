// swift-tools-version:5.9
// MPNet: MP NetWire status API for iOS / macOS apps, on top of Moya (fork: github.com/JebetrexxMC/Moya).
// MPNetNIO: direct UDP SA-MP / open.mp query on SwiftNIO (fork: github.com/JebetrexxMC/swift-nio).
import PackageDescription

let package = Package(
    name: "MPNet",
    platforms: [.iOS(.v13), .macOS(.v10_15)],
    products: [
        .library(name: "MPNet", targets: ["MPNet"]),
        .library(name: "MPNetNIO", targets: ["MPNetNIO"]),
    ],
    dependencies: [
        .package(url: "https://github.com/JebetrexxMC/Moya.git", branch: "master"),
        .package(url: "https://github.com/JebetrexxMC/swift-nio.git", branch: "main"),
    ],
    targets: [
        .target(name: "MPNet", dependencies: [.product(name: "Moya", package: "Moya")]),
        .target(name: "MPNetNIO", dependencies: [
            .product(name: "NIOCore", package: "swift-nio"),
            .product(name: "NIOPosix", package: "swift-nio"),
        ]),
        .testTarget(name: "MPNetTests", dependencies: ["MPNet"]),
        .testTarget(name: "MPNetNIOTests", dependencies: ["MPNetNIO", .product(name: "NIOCore", package: "swift-nio")]),
    ]
)
