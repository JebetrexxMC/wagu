// swift-tools-version:5.7
// MPNet: MP NetWire status API for iOS / macOS apps, on top of Moya (fork: github.com/JebetrexxMC/Moya).
import PackageDescription

let package = Package(
    name: "MPNet",
    platforms: [.iOS(.v13), .macOS(.v10_15)],
    products: [.library(name: "MPNet", targets: ["MPNet"])],
    dependencies: [
        .package(url: "https://github.com/JebetrexxMC/Moya.git", branch: "master"),
    ],
    targets: [
        .target(name: "MPNet", dependencies: [.product(name: "Moya", package: "Moya")]),
        .testTarget(name: "MPNetTests", dependencies: ["MPNet"]),
    ]
)
