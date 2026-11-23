# MPNet (Swift, Moya)

A Swift package for iOS and macOS apps. It reads the MP NetWire status (the same `wires.json` the website and the README badges use) through [Moya](https://github.com/Moya/Moya) (fork: [JebetrexxMC/Moya](https://github.com/JebetrexxMC/Moya)).

```swift
import MPNet
MPNetClient().status { result in
    if case .success(let s) = result {
        print("\(s.up)/\(s.probed) wires up, best region broker: \(s.best("region")?.name ?? "none")")
    }
}
```

Endpoints (`MPNetAPI`): `.wires`, `.history`, `.lobby` (MP lobby players and servers), `.crossplay` (MP Crossplay 24/7 address) and `.traffic(URL)` (an `mpnetd relay`/`watch` box).

Build and test on a Mac: `swift build && swift test`. CI builds it on a macOS runner (`.github/workflows/netwire-247.yml`, job `swift`). There is no MP iOS game client. This package is for status and companion apps.
