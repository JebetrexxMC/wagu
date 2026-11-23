// MPNet - the MP NetWire status API as a Moya target (iOS / macOS).
// Same files the website (mpsamp-open.github.io/network.html) and the README badges read.
import Foundation
import Moya

public enum MPNetAPI {
    case wires                  // netwire-status/wires.json  - every wire, grade, ping, 24 h uptime
    case history                // netwire-status/history.json - 288 rounds per wire
    case lobby                  // live-status/status.json    - MP lobby: players + servers
    case crossplay              // mp247-data/omp247.json     - MP Crossplay 24/7 address + players
    case traffic(URL)           // http://<relay box>:<port>  - mpnetd relay / watch /traffic.json
}

extension MPNetAPI: TargetType {
    public static var repo = "JebetrexxMC/wagu"

    public var baseURL: URL {
        if case .traffic(let u) = self { return u }
        return URL(string: "https://raw.githubusercontent.com/\(MPNetAPI.repo)")!
    }
    public var path: String {
        switch self {
        case .wires: return "/netwire-status/wires.json"
        case .history: return "/netwire-status/history.json"
        case .lobby: return "/live-status/status.json"
        case .crossplay: return "/mp247-data/omp247.json"
        case .traffic: return "/traffic.json"
        }
    }
    public var method: Moya.Method { .get }
    public var task: Moya.Task { .requestPlain }
    public var headers: [String: String]? { ["Accept": "application/json", "Cache-Control": "no-cache"] }
    public var sampleData: Data {
        switch self {
        case .wires: return Data(#"{"updated":0,"from":"sample","total":1,"probed":1,"up":1,"green":1,"best":{"lobby":"lobby-emqx"},"wires":[{"id":"lobby-emqx","group":"lobby","kind":"mqtt","host":"broker.emqx.io","port":1883,"name":"EMQX public lobby","grade":"green","rtt":57.6,"loss":0,"sent":5,"got":5}]}"#.utf8)
        default: return Data("{}".utf8)
        }
    }
}

// ---- models (wires.json) ----
public struct NetWireStatus: Codable {
    public let updated: Int
    public let from: String
    public let total, probed, up, green: Int
    public let best: [String: String]
    public let wires: [NetWire]
}

public struct NetWire: Codable, Identifiable {
    public let id, group, kind, host, name, grade: String
    public let port: Int
    public let rtt, min, max, jitter: Double?
    public let loss, sent, got: Int
    public let players: Int?
    public let maxPlayers: Int?
    public let server, info, error, region: String?
    public let uptime: Double?
    public let spark: String?

    enum CodingKeys: String, CodingKey {
        case id, group, kind, host, name, grade, port, rtt, min, max, jitter, loss, sent, got, players
        case maxPlayers = "max_players"
        case server, info, error, region, uptime, spark
    }
    public var isUp: Bool { grade != "down" }
}

public extension NetWireStatus {
    /// best wire of a group (lowest score the server worked out), e.g. "region", "lobby", "omp"
    func best(_ group: String) -> NetWire? { best[group].flatMap { id in wires.first { $0.id == id } } }
    /// the wires of a group, fastest first
    func group(_ g: String) -> [NetWire] { wires.filter { $0.group == g }.sorted { ($0.rtt ?? .infinity) < ($1.rtt ?? .infinity) } }
}

// ---- client ----
public final class MPNetClient {
    public let provider: MoyaProvider<MPNetAPI>
    public init(provider: MoyaProvider<MPNetAPI> = MoyaProvider<MPNetAPI>()) { self.provider = provider }

    public func status(_ done: @escaping (Result<NetWireStatus, Error>) -> Void) {
        provider.request(.wires) { r in
            switch r {
            case .success(let resp):
                do { done(.success(try JSONDecoder().decode(NetWireStatus.self, from: resp.filterSuccessfulStatusCodes().data))) }
                catch { done(.failure(error)) }
            case .failure(let e): done(.failure(e))
            }
        }
    }
}
