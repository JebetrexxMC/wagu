// swift-tools-version:5.9
// MPNetPulse: logs every MPNet (Moya) request to Pulse (fork: github.com/JebetrexxMC/Pulse), so a
// companion app can show the NetWire traffic in PulseUI. Separate package because Pulse needs
// iOS 15 / macOS 13 while MPNet itself still runs on iOS 13 / macOS 10.15.
import PackageDescription

let package = Package(
    name: "MPNetPulse",
    platforms: [.iOS(.v15), .macOS(.v13)],
    products: [.library(name: "MPNetPulse", targets: ["MPNetPulse"])],
    dependencies: [
        .package(path: "../swift"),
        .package(url: "https://github.com/JebetrexxMC/Pulse.git", branch: "main"),
        .package(url: "https://github.com/JebetrexxMC/Moya.git", branch: "master"),
    ],
    targets: [
        .target(name: "MPNetPulse", dependencies: [
            .product(name: "MPNet", package: "swift"),
            .product(name: "Pulse", package: "Pulse"),
            .product(name: "Moya", package: "Moya"),
        ]),
    ]
)
